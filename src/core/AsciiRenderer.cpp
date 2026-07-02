#include "AsciiRenderer.h"
#include "ColorMath.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QPainter>
#include <QtMath>
#include <cmath>
#include <vector>

namespace {

// Average linear-light luminance over the cell (0..1).
float cellLuminosity(const QImage& rgb, int cx, int cy, int cw, int ch)
{
    double sum = 0.0;
    int count = 0;
    for (int y = cy; y < cy + ch; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = cx; x < cx + cw; ++x) {
            sum += ColorMath::linearLuminance(line[x]);
            ++count;
        }
    }
    return count == 0 ? 1.0f : float(sum / count);
}

// Average colour over the cell in linear light, re-encoded to sRGB.
QColor cellAverageColor(const QImage& rgb, int cx, int cy, int cw, int ch)
{
    double r = 0.0, g = 0.0, b = 0.0;
    int count = 0;
    for (int y = cy; y < cy + ch; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = cx; x < cx + cw; ++x) {
            QRgb p = line[x];
            r += ColorMath::srgbToLinear(qRed(p));
            g += ColorMath::srgbToLinear(qGreen(p));
            b += ColorMath::srgbToLinear(qBlue(p));
            ++count;
        }
    }
    if (count == 0) return QColor(Qt::black);
    return QColor(ColorMath::linearToSrgb8(float(r / count)),
                  ColorMath::linearToSrgb8(float(g / count)),
                  ColorMath::linearToSrgb8(float(b / count)));
}

// Nearest palette colour (OkLab) to an sRGB colour, alpha from the tone.
QColor snapToPalette(const std::vector<ColorMath::PaletteEntry>& pal, const QColor& c)
{
    const ColorMath::OkLab lab = ColorMath::linearToOklab(
        ColorMath::srgbToLinear(c.red()),
        ColorMath::srgbToLinear(c.green()),
        ColorMath::srgbToLinear(c.blue()));
    return QColor::fromRgba(pal[ColorMath::nearestPaletteIndex(pal, lab)].out);
}

// Tonal-settings view shared by the ramp and braille paths: resolves the
// pen colour of a cell from its average colour / perceptual luminosity.
struct ToneCtx {
    bool imageColors = false;
    bool paletteMode = false;
    const std::vector<ToneEntry>* tones = nullptr;
    std::vector<ColorMath::PaletteEntry> palette;

    static ToneCtx make(const TonalSettings& t)
    {
        ToneCtx c;
        c.imageColors = (t.mode == ToneMode::ImageColors);
        c.paletteMode = (t.mode == ToneMode::Palette);
        c.tones       = &t.tones;
        if (c.paletteMode) c.palette = ColorMath::buildPalette(t.tones);
        return c;
    }

    QColor pen(const QImage& rgb, int cx, int cy, int cw, int ch, float lumPerc) const
    {
        if (paletteMode && !palette.empty())
            return snapToPalette(palette, cellAverageColor(rgb, cx, cy, cw, ch));
        if (imageColors || tones->empty())
            return cellAverageColor(rgb, cx, cy, cw, ch);
        const ToneEntry& te = (*tones)[pickToneIndex(*tones, lumPerc)];
        QColor pen = te.color;
        pen.setAlphaF(qBound(0.0f, te.opacity, 1.0f));
        return pen;
    }
};

// Font from the settings (family, weight, cell height). The coverage
// measurement adapts to any font, so non-monospace families work too.
QFont settingsFont(const AsciiSettings& params, int cellH)
{
    QFont font(params.fontFamily.isEmpty() ? QStringLiteral("Consolas")
                                           : params.fontFamily);
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(cellH);
    font.setWeight(QFont::Weight(qBound(100, params.fontWeight, 900)));
    return font;
}

// Measured ink coverage (0..1) of each glyph in the charset, for the given
// font and cell size. Rasterises each glyph once and counts covered pixels,
// so the luminosity→glyph mapping reflects each character's real visual
// weight instead of assuming the ramp is perceptually linear. Cached and
// normalised so the densest glyph maps to 1.0.
const std::vector<float>& glyphCoverage(const QFont& font, const QString& charset,
                                        int cellW, int cellH)
{
    static QMutex mutex;
    static QHash<QString, std::vector<float>> cache;
    QMutexLocker lock(&mutex);

    const QString key = font.family() + QLatin1Char('|')
                      + QString::number(font.weight()) + QLatin1Char('|') + charset
                      + QString("|%1x%2").arg(cellW).arg(cellH);
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    std::vector<float> cov(size_t(charset.size()), 0.0f);
    QImage glyph(cellW, cellH, QImage::Format_ARGB32);
    const double cellPixels = double(cellW) * cellH * 255.0;

    for (int i = 0; i < charset.size(); ++i) {
        glyph.fill(Qt::transparent);
        QPainter p(&glyph);
        p.setFont(font);
        p.setPen(Qt::white);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.drawText(QRectF(0, 0, cellW, cellH),
                   Qt::AlignCenter | Qt::TextDontClip, QString(charset.at(i)));
        p.end();

        double sum = 0.0;
        for (int y = 0; y < cellH; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(glyph.constScanLine(y));
            for (int x = 0; x < cellW; ++x) sum += qAlpha(line[x]);
        }
        cov[size_t(i)] = cellPixels > 0.0 ? float(sum / cellPixels) : 0.0f;
    }

    float maxc = 0.0f;
    for (float c : cov) maxc = qMax(maxc, c);
    if (maxc > 1e-4f) for (float& c : cov) c /= maxc;

    return cache.insert(key, std::move(cov)).value();
}

// Fill the cell with the pen, then punch the glyph out (inverse video —
// the glyph becomes a transparent window onto whatever is behind the layer).
void drawCell(QPainter& output, const QRectF& rect, const QChar& c,
              const QColor& pen, bool cellBackground)
{
    if (cellBackground) {
        output.fillRect(rect, pen);
        if (c != QChar(' ')) {
            output.setCompositionMode(QPainter::CompositionMode_Clear);
            output.drawText(rect, Qt::AlignCenter | Qt::TextDontClip, QString(c));
            output.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
    } else if (c != QChar(' ')) {
        output.setPen(pen);
        output.drawText(rect, Qt::AlignCenter | Qt::TextDontClip, QString(c));
    }
}

// ============================================================
//  Braille rendering — each cell becomes a 2×4 dot pattern
//  (U+2800..U+28FF), so the effective resolution is 8× the glyph
//  grid. Per-dot ordered thresholds keep tone in flat regions
//  (a tiny dither) while real image detail drives the dots.
// ============================================================

void renderBraille(const QImage& rgb, QPainter& output, const AsciiSettings& params)
{
    const int imgW  = rgb.width();
    const int imgH  = rgb.height();
    const int cellH = qBound(4, params.cellSize, 128);

    QFont font = settingsFont(params, cellH);
    if (!QFontMetricsF(font).inFontUcs4(0x28FF)) {
        font = QFont(QStringLiteral("Segoe UI Symbol"));   // braille-complete on Windows
        font.setPixelSize(cellH);
    }
    const QFontMetricsF fm(font);
    const int cellW = qMax(2, qRound(fm.horizontalAdvance(QChar(0x28FF))));
    const int cols  = (imgW + cellW - 1) / cellW;
    const int rows  = (imgH + cellH - 1) / cellH;

    const ToneCtx tc = ToneCtx::make(params.tonal);
    const float invGamma = 1.0f / qMax(0.01f, params.gamma);

    // Braille bit index per (row, col) of the 2×4 dot grid.
    static const int kDotBit[4][2] = { {0, 3}, {1, 4}, {2, 5}, {6, 7} };
    // Ordered per-dot thresholds (0..1), interleaved so flat tones become
    // even dot coverage instead of an all-or-nothing patch.
    static const float kDotThr[4][2] = {
        { 0.5f / 8, 6.5f / 8 },
        { 4.5f / 8, 2.5f / 8 },
        { 1.5f / 8, 7.5f / 8 },
        { 5.5f / 8, 3.5f / 8 },
    };

    output.save();
    output.setClipRect(0, 0, imgW, imgH);
    output.setFont(font);
    output.setRenderHint(QPainter::TextAntialiasing, true);

    for (int row = 0; row < rows; ++row) {
        const int cy = row * cellH;
        const int ch = qMin(cellH, imgH - cy);
        for (int col = 0; col < cols; ++col) {
            const int cx = col * cellW;
            const int cw = qMin(cellW, imgW - cx);

            int bits = 0;
            for (int dr = 0; dr < 4; ++dr) {
                const int y0 = cy + dr * ch / 4;
                const int y1 = cy + (dr + 1) * ch / 4;
                for (int dc = 0; dc < 2; ++dc) {
                    const int x0 = cx + dc * cw / 2;
                    const int x1 = cx + (dc + 1) * cw / 2;
                    if (x1 <= x0 || y1 <= y0) continue;
                    const float lum = cellLuminosity(rgb, x0, y0, x1 - x0, y1 - y0);
                    float darkness  = params.invert ? lum : 1.0f - lum;
                    darkness = std::pow(qBound(0.0f, darkness, 1.0f), invGamma);
                    if (darkness > kDotThr[dr][dc]) bits |= 1 << kDotBit[dr][dc];
                }
            }

            const QChar glyph(0x2800 + bits);
            if (bits == 0 && !params.cellBackground) continue;

            const float lumLin  = cellLuminosity(rgb, cx, cy, cw, ch);
            const float lumPerc = ColorMath::linearToSrgb8(lumLin) / 255.0f;
            const QColor pen    = tc.pen(rgb, cx, cy, cw, ch, lumPerc);
            drawCell(output, QRectF(cx, cy, cellW, cellH),
                     bits == 0 ? QChar(' ') : glyph, pen, params.cellBackground);
        }
    }

    output.restore();
}

} // namespace

void AsciiRenderer::render(const QImage& input, QPainter& output,
                           const AsciiSettings& params)
{
    if (input.isNull()) return;

    const QImage rgb = (input.format() == QImage::Format_RGB32)
                      ? input
                      : input.convertToFormat(QImage::Format_RGB32);

    if (params.isBraille()) {
        renderBraille(rgb, output, params);
        return;
    }

    const QString charset = params.effectiveCharset();
    const int     nChars  = charset.size();
    if (nChars < 2) return;

    const int imgW  = rgb.width();
    const int imgH  = rgb.height();
    const int cellH = qBound(3, params.cellSize, 128);

    const QFont font = settingsFont(params, cellH);

    // Real monospace advance width instead of a hardcoded ratio, so the
    // glyph grid matches the actual font metrics.
    const QFontMetricsF fm(font);
    const int cellW = qMax(2, qRound(fm.horizontalAdvance(QLatin1Char('M'))));
    const int cols  = (imgW + cellW - 1) / cellW;
    const int rows  = (imgH + cellH - 1) / cellH;

    const ToneCtx tc = ToneCtx::make(params.tonal);
    const float invGamma = 1.0f / qMax(0.01f, params.gamma);

    const std::vector<float>& coverage = glyphCoverage(font, charset, cellW, cellH);

    // Pass 1 — per-cell perceptual luminosity grid (also feeds the Sobel
    // pass, which needs neighbour cells).
    std::vector<float> lumLinGrid(size_t(cols) * rows);
    std::vector<float> lumPercGrid(size_t(cols) * rows);
    for (int row = 0; row < rows; ++row) {
        const int cy = row * cellH;
        const int ch = qMin(cellH, imgH - cy);
        for (int col = 0; col < cols; ++col) {
            const int cx = col * cellW;
            const int cw = qMin(cellW, imgW - cx);
            const float lumLin = cellLuminosity(rgb, cx, cy, cw, ch);
            lumLinGrid [size_t(row) * cols + col] = lumLin;
            lumPercGrid[size_t(row) * cols + col] =
                ColorMath::linearToSrgb8(lumLin) / 255.0f;
        }
    }

    // Edge pass — Sobel on the cell-luminosity grid. A cell whose gradient
    // magnitude clears the threshold is drawn as an oriented contour glyph
    // (- / | \) instead of a coverage glyph, tracing the image's outlines.
    const bool  edgesOn = params.edges > 0;
    const float edgeThr = 1.0f - 0.94f * qBound(0, params.edges, 100) / 100.0f;
    static const QChar kEdgeGlyphs[4] = {
        QLatin1Char('-'), QLatin1Char('/'), QLatin1Char('|'), QLatin1Char('\\')
    };
    auto lumAt = [&](int col, int row) -> float {
        return lumPercGrid[size_t(qBound(0, row, rows - 1)) * cols
                         + qBound(0, col, cols - 1)];
    };

    output.save();
    output.setClipRect(0, 0, imgW, imgH);
    output.setFont(font);
    output.setRenderHint(QPainter::Antialiasing, true);
    output.setRenderHint(QPainter::TextAntialiasing, true);

    for (int row = 0; row < rows; ++row) {
        const int cy = row * cellH;
        const int ch = qMin(cellH, imgH - cy);
        for (int col = 0; col < cols; ++col) {
            const int cx = col * cellW;
            const int cw = qMin(cellW, imgW - cx);

            const float lumLin  = lumLinGrid [size_t(row) * cols + col];
            const float lumPerc = lumPercGrid[size_t(row) * cols + col];
            float darkness      = params.invert ? lumLin : 1.0f - lumLin;
            darkness            = std::pow(qBound(0.0f, darkness, 1.0f), invGamma);

            QChar c;
            bool  isEdge = false;
            if (edgesOn) {
                const float gx =
                      lumAt(col + 1, row - 1) + 2.0f * lumAt(col + 1, row) + lumAt(col + 1, row + 1)
                    - lumAt(col - 1, row - 1) - 2.0f * lumAt(col - 1, row) - lumAt(col - 1, row + 1);
                const float gy =
                      lumAt(col - 1, row + 1) + 2.0f * lumAt(col, row + 1) + lumAt(col + 1, row + 1)
                    - lumAt(col - 1, row - 1) - 2.0f * lumAt(col, row - 1) - lumAt(col + 1, row - 1);
                const float mag = std::sqrt(gx * gx + gy * gy) / 4.0f;
                if (mag > edgeThr) {
                    // Edge direction = perpendicular to the gradient; y grows
                    // downward, so the sign of gy is flipped for screen angles.
                    float deg = qRadiansToDegrees(std::atan2(-gy, gx)) + 90.0f;
                    deg = std::fmod(std::fmod(deg, 180.0f) + 180.0f, 180.0f);
                    c = kEdgeGlyphs[qRound(deg / 45.0f) % 4];
                    isEdge = true;
                }
            }

            if (!isEdge) {
                // Pick the glyph whose measured ink coverage best matches the
                // target darkness (handles non-linear glyph weights).
                int   idx  = 0;
                float best = 2.0f;
                for (int i = 0; i < nChars; ++i) {
                    const float d = std::fabs(coverage[size_t(i)] - darkness);
                    if (d < best) { best = d; idx = i; }
                }
                c = charset.at(idx);
            }

            if (c == QChar(' ') && !params.cellBackground) continue;

            const QColor pen = tc.pen(rgb, cx, cy, cw, ch, lumPerc);
            drawCell(output, QRectF(cx, cy, cellW, cellH), c, pen,
                     params.cellBackground);
        }
    }

    output.restore();
}
