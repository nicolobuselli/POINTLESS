#include "DotGridRenderer.h"
#include "ColorMath.h"

#include <QPainter>
#include <QPen>
#include <QTransform>
#include <QSvgRenderer>
#include <QtConcurrent/QtConcurrent>
#include <QtMath>
#include <cmath>
#include <memory>

namespace {
constexpr int kBandHeight = 256;   // tile height for parallel rendering
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void DotGridRenderer::render(const QImage& input, QPainter& output,
                               const DotGridSettings& params)
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
    QtConcurrent::blockingMap(jobs, &DotGridRenderer::renderTile);

    output.save();
    output.setClipRect(0, 0, imgW, imgH);
    for (int b = 0; b < nBands; ++b)
        output.drawImage(0, b * kBandHeight, bandImages[b]);
    output.restore();
}

// Vector path: one job covering the whole image, painted straight onto `output`
// (no band rasters) so the dots stay as SVG shapes.
void DotGridRenderer::renderVector(const QImage& input, QPainter& output,
                                    const DotGridSettings& params)
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

    output.save();
    output.setRenderHint(QPainter::Antialiasing, true);
    TileJob job{ &inputRGB, &params, &samples, nullptr,
                 /*bandTop*/ 0, /*bandH*/ imgH, imgW, imgH };
    paintDots(output, job);
    output.restore();
}

int DotGridRenderer::estimateDotCount(const QImage& input, const DotGridSettings& params)
{
    if (input.isNull()) return 0;
    return int(GridGenerator::generate(params.grid, input.width(), input.height()).size());
}

// ---------------------------------------------------------------------------
// Per-tile rendering
// ---------------------------------------------------------------------------

void DotGridRenderer::renderTile(const TileJob& job)
{
    QImage& canvas = *job.canvas;
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw in absolute image coordinates; the band is a horizontal slice.
    painter.translate(0, -job.bandTop);
    painter.setClipRect(QRectF(0, job.bandTop, job.imgW, job.bandH));

    paintDots(painter, job);
}

// Dot grids (Square / Hexagonal / Radial): a shape per sample sized by luminance.
void DotGridRenderer::paintDots(QPainter& painter, const TileJob& job)
{
    const DotGridSettings& params      = *job.params;
    const QImage&           inputRGB    = *job.inputRGB;
    const auto&             samples     = *job.samples;
    const auto&             shapesVec   = params.shapes;
    const auto&             tones       = params.tonal.tones;
    const bool              imageColors = (params.tonal.mode == ToneMode::ImageColors);
    const int               imgW = job.imgW, imgH = job.imgH;
    const double            bandTop = job.bandTop;
    const double            bandBot = double(job.bandTop) + job.bandH;

    const float sp       = qMax(2.0f, params.grid.spacing);
    const float baseR    = (sp * 0.5f) * qMax(0.01f, params.grid.diameter);
    const float invGamma = 1.0f / qMax(0.01f, params.gamma);
    const int   cellPx   = qMax(1, qRound(sp));

    // Per-parameter localization: each enabled point turns a uniform slider
    // value into a spatial field (×scale inside its circle, base outside),
    // and ANY enabled point also masks the layer — dots only exist inside
    // the union of the circles, exactly like the original localize-diameter.
    const LocField locDia = locField(params.loc, LocParam::DgDiameter, imgW, imgH);
    const LocField locGam = locField(params.loc, LocParam::DgGamma,    imgW, imgH);
    const LocField locWgt = locField(params.loc, LocParam::DgWeight,   imgW, imgH);
    const LocField locJit = locField(params.loc, LocParam::DgJitter,   imgW, imgH);
    const LocMask  lmask  = locMask(params.loc, imgW, imgH);

    // Jitter orbits each symbol around an off-centroid pivot, so dots can drift
    // up to ~spacing away; widen the per-band cull by that reach to avoid seams.
    const float jitterMax   = params.jitter * (locJit.on ? qMax(1.0f, locJit.scale) : 1.0f);
    const float jitterReach = jitterMax * sp * 2.0f;

    // A localized scale > 1 can grow a dot past baseR: widen the band cull by
    // the largest possible multiplier so those dots aren't clipped at a seam.
    const float maxR = baseR * (locDia.on ? qMax(1.0f, locDia.scale) : 1.0f);

    std::unique_ptr<QSvgRenderer> svgCache;
    QString cachedSvgPath;

    for (const GridSample& s : samples) {
        if (s.y + maxR + jitterReach < bandTop
         || s.y - maxR - jitterReach > bandBot) continue;   // band cull (+ jitter drift)

        int cx, cy, cw, ch;
        cellAround(s.x, s.y, cellPx, imgW, imgH, cx, cy, cw, ch);
        const float lumLin   = sampleLuminosity(inputRGB, cx, cy, cw, ch);
        const float lumPerc  = ColorMath::linearToSrgb8(lumLin) / 255.0f;
        const float darkness = 1.0f - lumLin;
        const float invG     = locGam.on
            ? 1.0f / qMax(0.01f, params.gamma * locGam.mul(s.x, s.y)) : invGamma;
        const float cov      = std::pow(darkness, invG);
        const float locR     = baseR * locDia.mul(s.x, s.y) * lmask.mask(s.x, s.y);
        if (locR * std::sqrt(cov) < 0.5f) continue;     // cull empty cells by true coverage
        // weight lifts every visible symbol toward full size; at 1.0 all are equal.
        const float weight   = qBound(0.0f, params.weight * locWgt.mul(s.x, s.y), 1.0f);
        const float covW     = cov + weight * (1.0f - cov);
        const float r        = locR * std::sqrt(covW);  // dot area ∝ ink coverage

        DotGridShape shape   = shapesVec.empty() ? DotGridShape::Circle : shapesVec[0].shape;
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
        if (imageColors || tones.empty()) {
            fillColor = sampleAverageColor(inputRGB, cx, cy, cw, ch);
        } else {
            // FixedTones and Palette both map luminosity onto the tones via
            // their per-colour level thresholds (matches Dither/Ascii).
            const ToneEntry& te = tones[pickToneIndex(tones, lumPerc)];
            fillColor = te.color;
            fillColor.setAlphaF(qBound(0.0f, te.opacity, 1.0f));
        }
        fillColor.setAlphaF(fillColor.alphaF() * params.opacity);

        // Jitter = random spin about a pivot that drifts off the centroid. Both
        // the angle and the pivot distance grow with jitter, so the symbol both
        // rotates and orbits away from its grid point — gently at low values,
        // wildly at high ones.
        float   rotationDeg = 0.0f;
        QPointF pivot(0.0f, 0.0f);
        const float jit = qBound(0.0f, params.jitter * locJit.mul(s.x, s.y), 1.0f);
        if (jit > 0.0f) {
            // Hash-based rands shared with the GPU bake (was mt19937: same
            // statistics, different arrangement on old saved projects).
            float rr[3];
            cellJitterRands(qRound(s.x), qRound(s.y), rr);
            rotationDeg = (rr[0] * 2.0f - 1.0f) * 180.0f * jit;
            const float ang = rr[1] * 6.28318531f;            // random direction
            const float mag = rr[2] * jit * sp * 1.0f;        // distance ∝ jitter
            pivot = QPointF(std::cos(ang) * mag, std::sin(ang) * mag);
        }

        painter.save();
        if (shape == DotGridShape::CustomSVG && !svgPath.isEmpty()) {
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
                painter.translate(pivot.x(), pivot.y());   // rotate about an
                painter.rotate(rotationDeg);               // off-centroid pivot →
                painter.translate(-pivot.x(), -pivot.y()); // spin + drift
                painter.translate(-isize * 0.5f, -isize * 0.5f);
                painter.drawImage(QPointF(0, 0), svgImg);
            }
        } else {
            QPainterPath path = buildShape(shape, 0.0f, 0.0f, r, params.cornerRadius);
            QTransform t;
            t.translate(s.x, s.y);
            t.translate(pivot.x(), pivot.y());     // off-centroid pivot:
            t.rotate(rotationDeg);                 // the symbol spins and
            t.translate(-pivot.x(), -pivot.y());   // drifts off its grid point
            painter.setPen(Qt::NoPen);
            painter.setBrush(fillColor);
            painter.drawPath(t.map(path));
        }
        painter.restore();
    }
}

// ---------------------------------------------------------------------------
// GPU instances (Phase 3)
// ---------------------------------------------------------------------------

bool DotGridRenderer::gpuRenderable(const DotGridSettings& params)
{
    if (!params.tonal.enabled) return false;        // Fill "−": layer paints nothing
    if (params.shapes.size() > 8) return false;     // dot.vert UBO caps
    if (params.tonal.mode != ToneMode::ImageColors
        && params.tonal.tones.size() > 8) return false;
    for (const auto& sh : params.shapes)
        if (sh.shape == DotGridShape::CustomSVG) return false;
    return true;
}

float DotGridRenderer::gpuShapeId(DotGridShape s)
{
    switch (s) {
        case DotGridShape::Square:   return 1.0f;
        case DotGridShape::Triangle: return 2.0f;
        case DotGridShape::Star:     return 3.0f;
        default:                     return 0.0f;   // Circle
    }
}

void DotGridRenderer::cellJitterRands(int col, int row, float out[3])
{
    unsigned int h = cellSeed(col, row);
    for (int i = 0; i < 3; ++i) {                   // PCG-style output chain
        h = h * 747796405u + 2891336453u;
        unsigned int w = ((h >> ((h >> 28) + 4u)) ^ h) * 277803737u;
        w ^= w >> 22;
        out[i] = float(w >> 8) * (1.0f / 16777216.0f);   // 24-bit → [0,1)
    }
}

// ---------------------------------------------------------------------------
// Shape builders
// ---------------------------------------------------------------------------

// Build a closed polygon through `pts`, rounding each corner with a quadratic
// bezier whose control point is the original vertex. The radius is clamped per
// corner to half of the shorter adjacent edge so it never self-overlaps.
QPainterPath DotGridRenderer::roundedPolygon(const QVector<QPointF>& pts, float radius)
{
    const int n = pts.size();
    QPainterPath p;
    if (n < 3) return p;
    if (radius <= 0.0f) {
        p.moveTo(pts[0]);
        for (int i = 1; i < n; ++i) p.lineTo(pts[i]);
        p.closeSubpath();
        return p;
    }
    for (int i = 0; i < n; ++i) {
        const QPointF curr = pts[i];
        const QPointF prev = pts[(i - 1 + n) % n];
        const QPointF next = pts[(i + 1) % n];
        const QPointF v1 = prev - curr, v2 = next - curr;
        const double  l1 = std::hypot(v1.x(), v1.y());
        const double  l2 = std::hypot(v2.x(), v2.y());
        if (l1 < 1e-3 || l2 < 1e-3) continue;
        const double  r = qMin<double>(radius, qMin(l1, l2) * 0.5);
        const QPointF p1 = curr + v1 * (r / l1);   // enter corner along prev edge
        const QPointF p2 = curr + v2 * (r / l2);   // leave corner along next edge
        if (i == 0) p.moveTo(p1);
        else        p.lineTo(p1);
        p.quadTo(curr, p2);
    }
    p.closeSubpath();
    return p;
}

QPainterPath DotGridRenderer::buildTriangle(float cx, float cy, float r, float cornerRadius)
{
    constexpr float k2pi3 = 2.0f * static_cast<float>(M_PI) / 3.0f;
    const float a0 = -static_cast<float>(M_PI_2);
    QVector<QPointF> pts;
    for (int i = 0; i < 3; ++i)
        pts.append({ cx + r * std::cos(a0 + i * k2pi3),
                     cy + r * std::sin(a0 + i * k2pi3) });
    return roundedPolygon(pts, cornerRadius);
}

QPainterPath DotGridRenderer::buildCircle(float cx, float cy, float r)
{
    QPainterPath p;
    p.addEllipse(QPointF(cx, cy), r, r);
    return p;
}

QPainterPath DotGridRenderer::buildSquare(float cx, float cy, float r, float cornerRadius)
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

QPainterPath DotGridRenderer::buildStar(float cx, float cy, float r, float cornerRadius, int points)
{
    const float innerR = r * 0.4f;
    const float step   = static_cast<float>(M_PI) / points;
    QVector<QPointF> pts;
    for (int i = 0; i < points * 2; ++i) {
        const float angle = i * step - static_cast<float>(M_PI_2);
        const float rad   = (i % 2 == 0) ? r : innerR;
        pts.append({ cx + std::cos(angle) * rad, cy + std::sin(angle) * rad });
    }
    return roundedPolygon(pts, cornerRadius);
}

QPainterPath DotGridRenderer::buildShape(DotGridShape shape, float cx, float cy, float r,
                                           float cornerRadius)
{
    switch (shape) {
        case DotGridShape::Triangle: return buildTriangle(cx, cy, r, cornerRadius);
        case DotGridShape::Circle:   return buildCircle  (cx, cy, r);
        case DotGridShape::Square:   return buildSquare  (cx, cy, r, cornerRadius);
        case DotGridShape::Star:     return buildStar    (cx, cy, r, cornerRadius);
        default:                      return buildCircle  (cx, cy, r);
    }
}

// ---------------------------------------------------------------------------
// Sampling helpers
// ---------------------------------------------------------------------------

// Average linear-light luminance over the cell (0..1).
float DotGridRenderer::sampleLuminosity(const QImage& rgb,
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
QColor DotGridRenderer::sampleAverageColor(const QImage& rgb,
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
void DotGridRenderer::cellAround(float sx, float sy, int cellPx, int imgW, int imgH,
                                  int& cx, int& cy, int& cw, int& ch)
{
    cellPx = qMax(1, cellPx);
    cx = qBound(0, qRound(sx) - cellPx / 2, qMax(0, imgW - 1));
    cy = qBound(0, qRound(sy) - cellPx / 2, qMax(0, imgH - 1));
    cw = qBound(1, cellPx, imgW - cx);
    ch = qBound(1, cellPx, imgH - cy);
}

unsigned int DotGridRenderer::cellSeed(int col, int row)
{
    unsigned int h = static_cast<unsigned int>(col * 73856093 ^ row * 19349663);
    h ^= (h >> 16);
    h *= 0x45d9f3b;
    h ^= (h >> 16);
    return h;
}
