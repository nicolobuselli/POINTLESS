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

    const QString key = font.family() + QLatin1Char('|') + charset
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

} // namespace

void AsciiRenderer::render(const QImage& input, QPainter& output,
                           const AsciiSettings& params)
{
    if (input.isNull()) return;

    const QString charset = params.effectiveCharset();
    const int     nChars  = charset.size();
    if (nChars < 2) return;

    const QImage rgb = (input.format() == QImage::Format_RGB32)
                      ? input
                      : input.convertToFormat(QImage::Format_RGB32);

    const int imgW  = rgb.width();
    const int imgH  = rgb.height();
    const int cellH = qBound(3, params.cellSize, 128);

    QFont font("Consolas");
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(cellH);
    font.setWeight(QFont::DemiBold);

    // Real monospace advance width instead of a hardcoded ratio, so the
    // glyph grid matches the actual font metrics.
    const QFontMetricsF fm(font);
    const int cellW = qMax(2, qRound(fm.horizontalAdvance(QLatin1Char('M'))));
    const int cols  = (imgW + cellW - 1) / cellW;
    const int rows  = (imgH + cellH - 1) / cellH;

    const bool  imageColors = (params.tonal.mode == ToneMode::ImageColors);
    const bool  paletteMode = (params.tonal.mode == ToneMode::Palette);
    const auto& tones       = params.tonal.tones;
    const std::vector<ColorMath::PaletteEntry> palette =
        paletteMode ? ColorMath::buildPalette(tones)
                    : std::vector<ColorMath::PaletteEntry>{};
    const float invGamma    = 1.0f / qMax(0.01f, params.gamma);

    const std::vector<float>& coverage = glyphCoverage(font, charset, cellW, cellH);

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

            const float lumLin  = cellLuminosity(rgb, cx, cy, cw, ch);
            const float lumPerc = ColorMath::linearToSrgb8(lumLin) / 255.0f;
            float darkness      = params.invert ? lumLin : 1.0f - lumLin;
            darkness            = std::pow(qBound(0.0f, darkness, 1.0f), invGamma);

            // Pick the glyph whose measured ink coverage best matches the
            // target darkness (handles non-linear glyph weights).
            int   idx  = 0;
            float best = 2.0f;
            for (int i = 0; i < nChars; ++i) {
                const float d = std::fabs(coverage[size_t(i)] - darkness);
                if (d < best) { best = d; idx = i; }
            }
            const QChar c = charset.at(idx);
            if (c == QChar(' ')) continue;   // blank cell → background

            QColor pen;
            if (paletteMode && !palette.empty()) {
                pen = snapToPalette(palette, cellAverageColor(rgb, cx, cy, cw, ch));
            } else if (imageColors || tones.empty()) {
                pen = cellAverageColor(rgb, cx, cy, cw, ch);
            } else {
                const ToneEntry& te = tones[pickToneIndex(tones, lumPerc)];
                pen = te.color;
                pen.setAlphaF(qBound(0.0f, te.opacity, 1.0f));
            }

            output.setPen(pen);
            output.drawText(QRectF(cx, cy, cellW, cellH),
                            Qt::AlignCenter | Qt::TextDontClip, QString(c));
        }
    }

    output.restore();
}
