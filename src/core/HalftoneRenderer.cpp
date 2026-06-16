#include "HalftoneRenderer.h"
#include "ColorMath.h"

#include <QPainter>
#include <QPen>
#include <QTransform>
#include <QSvgRenderer>
#include <QtConcurrent/QtConcurrent>
#include <QtMath>
#include <cmath>
#include <memory>
#include <random>

namespace {
constexpr int kBandHeight = 256;   // tile height for parallel rendering

// Nearest palette colour (OkLab) to an sRGB colour, alpha from the tone.
QColor snapToPalette(const std::vector<ColorMath::PaletteEntry>& pal, const QColor& c)
{
    const ColorMath::OkLab lab = ColorMath::linearToOklab(
        ColorMath::srgbToLinear(c.red()),
        ColorMath::srgbToLinear(c.green()),
        ColorMath::srgbToLinear(c.blue()));
    return QColor::fromRgba(pal[ColorMath::nearestPaletteIndex(pal, lab)].out);
}
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void HalftoneRenderer::render(const QImage& input, QPainter& output,
                               const HalftoneSettings& params)
{
    if (input.isNull()) return;

    const int imgW = input.width();
    const int imgH = input.height();

    const QImage inputRGB = (input.format() == QImage::Format_RGB32)
                           ? input
                           : input.convertToFormat(QImage::Format_RGB32);

    const std::vector<GridSample> samples =
        GridGenerator::generate(params.grid, imgW, imgH);
    if (samples.empty()) return;

    const int nBands = (imgH + kBandHeight - 1) / kBandHeight;

    QVector<QImage>  bandImages(nBands);
    QVector<TileJob> jobs(nBands);
    for (int b = 0; b < nBands; ++b) {
        const int top = b * kBandHeight;
        const int h   = qMin(kBandHeight, imgH - top);
        bandImages[b] = QImage(imgW, h, QImage::Format_ARGB32_Premultiplied);
        bandImages[b].fill(Qt::transparent);
        jobs[b] = { &inputRGB, &params, &samples, &bandImages[b], top, h, imgW, imgH };
    }

    // blockingMap runs synchronously — `samples` stays valid for all jobs.
    QtConcurrent::blockingMap(jobs, &HalftoneRenderer::renderTile);

    output.save();
    output.setClipRect(0, 0, imgW, imgH);
    for (int b = 0; b < nBands; ++b)
        output.drawImage(0, b * kBandHeight, bandImages[b]);
    output.restore();
}

// ---------------------------------------------------------------------------
// Per-tile rendering
// ---------------------------------------------------------------------------

void HalftoneRenderer::renderTile(const TileJob& job)
{
    QImage& canvas = *job.canvas;
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw in absolute image coordinates; the band is a horizontal slice.
    painter.translate(0, -job.bandTop);
    painter.setClipRect(QRectF(0, job.bandTop, job.imgW, job.bandH));

    const GridType type = job.params->grid.type;
    if (type == GridType::Line || type == GridType::Circles)
        paintStrokes(painter, job);
    else
        paintDots(painter, job);
}

// Dot grids (Square / Hexagonal / Radial): a shape per sample sized by luminance.
void HalftoneRenderer::paintDots(QPainter& painter, const TileJob& job)
{
    const HalftoneSettings& params      = *job.params;
    const QImage&           inputRGB    = *job.inputRGB;
    const auto&             samples     = *job.samples;
    const auto&             shapesVec   = params.shapes;
    const auto&             tones       = params.tonal.tones;
    const bool              imageColors = (params.tonal.mode == ToneMode::ImageColors);
    const bool              paletteMode = (params.tonal.mode == ToneMode::Palette);
    const std::vector<ColorMath::PaletteEntry> palette =
        paletteMode ? ColorMath::buildPalette(tones)
                    : std::vector<ColorMath::PaletteEntry>{};
    const int               imgW = job.imgW, imgH = job.imgH;
    const double            bandTop = job.bandTop;
    const double            bandBot = double(job.bandTop) + job.bandH;

    const float sp       = qMax(2.0f, params.grid.spacing);
    const float baseR    = (sp * 0.5f) * qMax(0.01f, params.grid.diameter);
    const float invGamma = 1.0f / qMax(0.01f, params.gamma);
    const int   cellPx   = qMax(1, qRound(sp));

    std::unique_ptr<QSvgRenderer> svgCache;
    QString cachedSvgPath;

    for (const GridSample& s : samples) {
        if (s.y + baseR < bandTop || s.y - baseR > bandBot) continue;   // band cull

        int cx, cy, cw, ch;
        cellAround(s.x, s.y, cellPx, imgW, imgH, cx, cy, cw, ch);
        const float lumLin   = sampleLuminosity(inputRGB, cx, cy, cw, ch);
        const float lumPerc  = ColorMath::linearToSrgb8(lumLin) / 255.0f;
        const float darkness = 1.0f - lumLin;
        const float cov      = std::pow(darkness, invGamma);
        const float r        = baseR * std::sqrt(cov);   // dot area ∝ ink coverage
        if (r < 0.5f) continue;

        HalftoneShape shape   = shapesVec.empty() ? HalftoneShape::Circle : shapesVec[0].shape;
        QString       svgPath = shapesVec.empty() ? QString()             : shapesVec[0].svgPath;
        if (shapesVec.size() > 1) {
            const int lumInt = int(lumPerc * 255.f);
            const int n      = int(shapesVec.size());
            const int bias   = params.multiThreshold - 128;
            const int adj    = qBound(0, lumInt + bias, 255);
            const int idx    = qBound(0, adj * n / 256, n - 1);
            shape   = shapesVec[idx].shape;
            svgPath = shapesVec[idx].svgPath;
        }

        QColor fillColor;
        if (paletteMode && !palette.empty()) {
            fillColor = snapToPalette(palette, sampleAverageColor(inputRGB, cx, cy, cw, ch));
        } else if (imageColors || tones.empty()) {
            fillColor = sampleAverageColor(inputRGB, cx, cy, cw, ch);
        } else {
            const ToneEntry& te = tones[pickToneIndex(tones, lumPerc)];
            fillColor = te.color;
            fillColor.setAlphaF(qBound(0.0f, te.opacity, 1.0f));
        }
        fillColor.setAlphaF(fillColor.alphaF() * params.opacity);

        float rotationDeg = 0.0f;
        if (params.jitter > 0.0f) {
            std::mt19937 rng(cellSeed(qRound(s.x), qRound(s.y)));
            std::uniform_real_distribution<float> dist(-180.0f, 180.0f);
            rotationDeg = dist(rng) * params.jitter;
        }

        painter.save();
        if (shape == HalftoneShape::CustomSVG && !svgPath.isEmpty()) {
            if (cachedSvgPath != svgPath) {
                svgCache      = std::make_unique<QSvgRenderer>(svgPath);
                cachedSvgPath = svgPath;
            }
            if (svgCache && svgCache->isValid()) {
                const int isize = qMax(1, qCeil(r * 2.0f));
                QImage svgImg(isize, isize, QImage::Format_ARGB32_Premultiplied);
                svgImg.fill(Qt::transparent);
                {
                    QPainter sp2(&svgImg);
                    sp2.setRenderHint(QPainter::Antialiasing, true);
                    svgCache->render(&sp2, QRectF(0, 0, isize, isize));
                }
                {
                    QPainter cp(&svgImg);
                    cp.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    cp.fillRect(svgImg.rect(), fillColor);
                }
                // Compose on top of the band translate (no setTransform reset).
                painter.translate(s.x, s.y);
                painter.rotate(rotationDeg);
                painter.translate(-isize * 0.5f, -isize * 0.5f);
                painter.drawImage(QPointF(0, 0), svgImg);
            }
        } else {
            QPainterPath path = buildShape(shape, 0.0f, 0.0f, r, params.cornerRadius);
            QTransform t;
            t.translate(s.x, s.y);
            t.rotate(rotationDeg);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fillColor);
            painter.drawPath(t.map(path));
        }
        painter.restore();
    }
}

// Line / Circles: connect consecutive samples of each structure with a
// variable-width stroke — dark regions thicken the line, light ones thin it.
void HalftoneRenderer::paintStrokes(QPainter& painter, const TileJob& job)
{
    const HalftoneSettings& params      = *job.params;
    const QImage&           inputRGB    = *job.inputRGB;
    const auto&             samples     = *job.samples;
    const auto&             tones       = params.tonal.tones;
    const bool              imageColors = (params.tonal.mode == ToneMode::ImageColors);
    const bool              paletteMode = (params.tonal.mode == ToneMode::Palette);
    const std::vector<ColorMath::PaletteEntry> palette =
        paletteMode ? ColorMath::buildPalette(tones)
                    : std::vector<ColorMath::PaletteEntry>{};
    const bool              closed      = (params.grid.type == GridType::Circles);
    const int               imgW = job.imgW, imgH = job.imgH;
    const double            bandTop = job.bandTop;
    const double            bandBot = double(job.bandTop) + job.bandH;

    const float sp       = qMax(2.0f, params.grid.spacing);
    const float maxW     = sp * qMax(0.01f, params.grid.diameter);
    const float invGamma = 1.0f / qMax(0.01f, params.gamma);
    const float halfMax  = maxW * 0.5f;
    const int   cellPx   = qMax(2, qRound(qMin(params.grid.pointSpacing, params.grid.spacing)));

    auto sampleAt = [&](const GridSample& s, float& w, QColor& col) {
        int cx, cy, cw, ch;
        cellAround(s.x, s.y, cellPx, imgW, imgH, cx, cy, cw, ch);
        const float lumLin   = sampleLuminosity(inputRGB, cx, cy, cw, ch);
        const float lumPerc  = ColorMath::linearToSrgb8(lumLin) / 255.0f;
        const float darkness = 1.0f - lumLin;
        w = maxW * std::pow(darkness, invGamma);   // stroke area ∝ width ∝ coverage
        if (paletteMode && !palette.empty()) {
            col = snapToPalette(palette, sampleAverageColor(inputRGB, cx, cy, cw, ch));
        } else if (imageColors || tones.empty()) {
            col = sampleAverageColor(inputRGB, cx, cy, cw, ch);
        } else {
            const ToneEntry& te = tones[pickToneIndex(tones, lumPerc)];
            col = te.color;
            col.setAlphaF(qBound(0.0f, te.opacity, 1.0f));
        }
        col.setAlphaF(col.alphaF() * params.opacity);
    };

    painter.setBrush(Qt::NoBrush);

    const size_t N = samples.size();
    size_t i = 0;
    while (i < N) {
        const int structure = samples[i].structure;
        size_t j = i;
        while (j < N && samples[j].structure == structure) ++j;
        const size_t cnt = j - i;

        if (cnt >= 2) {
            const size_t segs = closed ? cnt : cnt - 1;
            for (size_t k = 0; k < segs; ++k) {
                const GridSample& a = samples[i + k];
                const GridSample& b = samples[i + ((k + 1) % cnt)];

                const double ymin = qMin(a.y, b.y) - halfMax;
                const double ymax = qMax(a.y, b.y) + halfMax;
                if (ymax < bandTop || ymin > bandBot) continue;   // band cull

                float wa, wb; QColor ca, cb;
                sampleAt(a, wa, ca);
                sampleAt(b, wb, cb);
                const float w = (wa + wb) * 0.5f;
                if (w < 0.3f) continue;                           // light → no stroke

                QPen pen(cb);
                pen.setWidthF(w);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(pen);
                painter.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
            }
        }
        i = j;
    }
}

// ---------------------------------------------------------------------------
// Shape builders
// ---------------------------------------------------------------------------

QPainterPath HalftoneRenderer::buildTriangle(float cx, float cy, float r)
{
    QPainterPath p;
    constexpr float k2pi3 = 2.0f * static_cast<float>(M_PI) / 3.0f;
    float a0 = -static_cast<float>(M_PI_2);
    p.moveTo(cx + r * std::cos(a0),          cy + r * std::sin(a0));
    p.lineTo(cx + r * std::cos(a0 + k2pi3),  cy + r * std::sin(a0 + k2pi3));
    p.lineTo(cx + r * std::cos(a0 + 2*k2pi3), cy + r * std::sin(a0 + 2*k2pi3));
    p.closeSubpath();
    return p;
}

QPainterPath HalftoneRenderer::buildCircle(float cx, float cy, float r)
{
    QPainterPath p;
    p.addEllipse(QPointF(cx, cy), r, r);
    return p;
}

QPainterPath HalftoneRenderer::buildSquare(float cx, float cy, float r, float cornerRadius)
{
    QPainterPath p;
    if (cornerRadius > 0.0f) {
        float cr = qMin(cornerRadius, r);
        p.addRoundedRect(cx - r, cy - r, r * 2.0f, r * 2.0f, cr, cr);
    } else {
        p.addRect(cx - r, cy - r, r * 2.0f, r * 2.0f);
    }
    return p;
}

QPainterPath HalftoneRenderer::buildStar(float cx, float cy, float r, int points)
{
    QPainterPath p;
    float innerR = r * 0.4f;
    float step   = static_cast<float>(M_PI) / points;
    for (int i = 0; i < points * 2; ++i) {
        float angle = i * step - static_cast<float>(M_PI_2);
        float rad   = (i % 2 == 0) ? r : innerR;
        float x     = cx + std::cos(angle) * rad;
        float y     = cy + std::sin(angle) * rad;
        if (i == 0) p.moveTo(x, y);
        else        p.lineTo(x, y);
    }
    p.closeSubpath();
    return p;
}

QPainterPath HalftoneRenderer::buildShape(HalftoneShape shape, float cx, float cy, float r,
                                           float cornerRadius)
{
    switch (shape) {
        case HalftoneShape::Triangle: return buildTriangle(cx, cy, r);
        case HalftoneShape::Circle:   return buildCircle  (cx, cy, r);
        case HalftoneShape::Square:   return buildSquare  (cx, cy, r, cornerRadius);
        case HalftoneShape::Star:     return buildStar    (cx, cy, r);
        default:                      return buildCircle  (cx, cy, r);
    }
}

// ---------------------------------------------------------------------------
// Sampling helpers
// ---------------------------------------------------------------------------

// Average linear-light luminance over the cell (0..1).
float HalftoneRenderer::sampleLuminosity(const QImage& rgb,
                                          int cellX, int cellY,
                                          int cellW, int cellH)
{
    double sum   = 0.0;
    int    count = 0;
    for (int y = cellY; y < cellY + cellH; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = cellX; x < cellX + cellW; ++x) {
            sum += ColorMath::linearLuminance(line[x]);
            ++count;
        }
    }
    return count == 0 ? 1.0f : static_cast<float>(sum / count);
}

// Average colour over the cell, computed in linear light then re-encoded
// to sRGB (a plain byte average would skew toward the darker side).
QColor HalftoneRenderer::sampleAverageColor(const QImage& rgb,
                                             int cellX, int cellY,
                                             int cellW, int cellH)
{
    double r = 0.0, g = 0.0, b = 0.0;
    int count = 0;
    for (int y = cellY; y < cellY + cellH; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = cellX; x < cellX + cellW; ++x) {
            QRgb px = line[x];
            r += ColorMath::srgbToLinear(qRed(px));
            g += ColorMath::srgbToLinear(qGreen(px));
            b += ColorMath::srgbToLinear(qBlue(px));
            ++count;
        }
    }
    if (count == 0) return Qt::black;
    return QColor(ColorMath::linearToSrgb8(float(r / count)),
                  ColorMath::linearToSrgb8(float(g / count)),
                  ColorMath::linearToSrgb8(float(b / count)));
}

// Clamped sampling cell of side `cellPx` centred on (sx, sy).
void HalftoneRenderer::cellAround(float sx, float sy, int cellPx, int imgW, int imgH,
                                  int& cx, int& cy, int& cw, int& ch)
{
    cellPx = qMax(1, cellPx);
    cx = qBound(0, qRound(sx) - cellPx / 2, qMax(0, imgW - 1));
    cy = qBound(0, qRound(sy) - cellPx / 2, qMax(0, imgH - 1));
    cw = qBound(1, cellPx, imgW - cx);
    ch = qBound(1, cellPx, imgH - cy);
}

unsigned int HalftoneRenderer::cellSeed(int col, int row)
{
    unsigned int h = static_cast<unsigned int>(col * 73856093 ^ row * 19349663);
    h ^= (h >> 16);
    h *= 0x45d9f3b;
    h ^= (h >> 16);
    return h;
}
