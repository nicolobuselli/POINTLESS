#include "HalftoneRenderer.h"

#include <QPainter>
#include <QTransform>
#include <QSvgRenderer>
#include <QtConcurrent/QtConcurrent>
#include <QtMath>
#include <cmath>
#include <memory>
#include <random>

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void HalftoneRenderer::render(const QImage& input, QPainter& output,
                               const HalftoneSettings& params)
{
    if (input.isNull()) return;

    const int imgW = input.width();
    const int imgH = input.height();
    const int gs   = qMax(2, params.gridSize);
    const int cols = (imgW + gs - 1) / gs;
    const int rows = (imgH + gs - 1) / gs;

    const QImage inputRGB = (input.format() == QImage::Format_RGB32)
                           ? input
                           : input.convertToFormat(QImage::Format_RGB32);

    // Padding lets shapes extend beyond their grid row without being clipped.
    const int padding = qRound(params.symbolSize * gs) + 2;

    QVector<QImage> rowImages(rows);
    for (int r = 0; r < rows; ++r) {
        int cellY = r * gs;
        int cellH = qMin(gs, imgH - cellY);
        rowImages[r] = QImage(imgW, cellH + 2 * padding, QImage::Format_ARGB32_Premultiplied);
        rowImages[r].fill(Qt::transparent);
    }

    QVector<RowJob> jobs(rows);
    for (int r = 0; r < rows; ++r) {
        jobs[r] = { &input, &inputRGB, &rowImages[r], &params, r, cols, gs, padding };
    }

    QtConcurrent::blockingMap(jobs, &HalftoneRenderer::renderRow);

    output.save();
    output.setClipRect(0, 0, imgW, imgH);
    for (int r = 0; r < rows; ++r) {
        int cellY = r * gs;
        output.drawImage(0, cellY - padding, rowImages[r]);
    }
    output.restore();
}

// ---------------------------------------------------------------------------
// Per-row rendering
// ---------------------------------------------------------------------------

void HalftoneRenderer::renderRow(const RowJob& job)
{
    const QImage&           input    = *job.input;
    const QImage&           inputRGB = *job.inputRGB;
    QImage&                 canvas   = *job.canvas;
    const HalftoneSettings& params   = *job.params;

    const int gs      = job.gs;
    const int row     = job.row;
    const int cols    = job.totalCols;
    const int padding = job.padding;
    const int imgW    = input.width();
    const int imgH    = input.height();
    Q_UNUSED(imgH);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);

    std::unique_ptr<QSvgRenderer> svgRendererCache;
    QString cachedSvgPath;

    const auto& shapesVec = params.shapes;
    const auto& tones     = params.tonal.tones;
    const bool  imageColors = (params.tonal.mode == ToneMode::ImageColors);

    for (int col = 0; col < cols; ++col) {
        int cellX = col * gs;
        int cellY = row * gs;
        int cellW = qMin(gs, imgW - cellX);
        int cellH = qMin(gs, input.height() - cellY);

        float cx = cellX + cellW * 0.5f;
        float cy = cellH * 0.5f + padding;

        float lum      = sampleLuminosity(inputRGB, cellX, cellY, cellW, cellH);
        float baseR    = (gs * 0.5f) * params.symbolSize;
        float darkness = 1.0f - lum;
        float scale    = std::pow(darkness, 1.0f / qMax(0.01f, params.gamma));
        float r        = baseR * scale;

        if (r < 0.5f) continue;

        // Shape selection
        HalftoneShape shape   = shapesVec.empty() ? HalftoneShape::Circle : shapesVec[0].shape;
        QString       svgPath = shapesVec.empty() ? QString()              : shapesVec[0].svgPath;

        if (shapesVec.size() > 1) {
            int lumInt = static_cast<int>(lum * 255.f);
            int n      = static_cast<int>(shapesVec.size());
            int bias   = params.multiThreshold - 128;
            int adj    = qBound(0, lumInt + bias, 255);
            int idx    = qBound(0, adj * n / 256, n - 1);
            shape   = shapesVec[idx].shape;
            svgPath = shapesVec[idx].svgPath;
        }

        // Fill color: image colors or tonal mapping
        QColor fillColor;
        if (imageColors || tones.empty()) {
            fillColor = sampleAverageColor(inputRGB, cellX, cellY, cellW, cellH);
        } else {
            fillColor = tones[pickToneIndex(tones, lum)].color;
        }
        fillColor.setAlphaF(fillColor.alphaF() * params.opacity);

        // Jitter
        float rotationDeg = 0.0f;
        if (params.jitter > 0.0f) {
            std::mt19937 rng(cellSeed(col, row));
            std::uniform_real_distribution<float> dist(-180.0f, 180.0f);
            rotationDeg = dist(rng) * params.jitter;
        }

        painter.save();

        if (shape == HalftoneShape::CustomSVG && !svgPath.isEmpty()) {
            if (cachedSvgPath != svgPath) {
                svgRendererCache = std::make_unique<QSvgRenderer>(svgPath);
                cachedSvgPath    = svgPath;
            }
            if (svgRendererCache && svgRendererCache->isValid()) {
                int isize = qMax(1, qCeil(r * 2.0f));

                // Render SVG into temp image then colorize with fillColor
                QImage svgImg(isize, isize, QImage::Format_ARGB32_Premultiplied);
                svgImg.fill(Qt::transparent);
                {
                    QPainter sp(&svgImg);
                    sp.setRenderHint(QPainter::Antialiasing, true);
                    svgRendererCache->render(&sp, QRectF(0, 0, isize, isize));
                }
                {
                    QPainter cp(&svgImg);
                    cp.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    cp.fillRect(svgImg.rect(), fillColor);
                }

                QTransform t;
                t.translate(cx, cy);
                t.rotate(rotationDeg);
                t.translate(-isize * 0.5f, -isize * 0.5f);
                painter.setTransform(t);
                painter.setOpacity(1.0);
                painter.drawImage(QPointF(0, 0), svgImg);
            }
        } else {
            QPainterPath path = buildShape(shape, 0.0f, 0.0f, r, params.cornerRadius);

            QTransform t;
            t.translate(cx, cy);
            t.rotate(rotationDeg);
            QPainterPath transformed = t.map(path);

            painter.setPen(Qt::NoPen);
            painter.setBrush(fillColor);
            painter.drawPath(transformed);
        }

        painter.restore();
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

float HalftoneRenderer::sampleLuminosity(const QImage& rgb,
                                          int cellX, int cellY,
                                          int cellW, int cellH)
{
    double sum   = 0.0;
    int    count = 0;
    for (int y = cellY; y < cellY + cellH; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = cellX; x < cellX + cellW; ++x) {
            QRgb px = line[x];
            sum += 0.2126 * qRed(px) + 0.7152 * qGreen(px) + 0.0722 * qBlue(px);
            ++count;
        }
    }
    return count == 0 ? 1.0f : static_cast<float>(sum / (count * 255.0));
}

QColor HalftoneRenderer::sampleAverageColor(const QImage& rgb,
                                             int cellX, int cellY,
                                             int cellW, int cellH)
{
    long long r = 0, g = 0, b = 0;
    int count = 0;
    for (int y = cellY; y < cellY + cellH; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = cellX; x < cellX + cellW; ++x) {
            QRgb px = line[x];
            r += qRed(px); g += qGreen(px); b += qBlue(px);
            ++count;
        }
    }
    return count == 0 ? Qt::black : QColor(r/count, g/count, b/count);
}

unsigned int HalftoneRenderer::cellSeed(int col, int row)
{
    unsigned int h = static_cast<unsigned int>(col * 73856093 ^ row * 19349663);
    h ^= (h >> 16);
    h *= 0x45d9f3b;
    h ^= (h >> 16);
    return h;
}
