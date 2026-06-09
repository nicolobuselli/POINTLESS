#include "HalftoneRenderer.h"

#include <QPainter>
#include <QTransform>
#include <QSvgRenderer>
#include <QtConcurrent/QtConcurrent>
#include <QtMath>
#include <cmath>
#include <random>

// ---------------------------------------------------------------------------
// Public entry point — splits work into row jobs, runs in parallel
// ---------------------------------------------------------------------------

void HalftoneRenderer::render(const QImage& input, QPainter& output,
                               const HalftoneParams& params)
{
    if (input.isNull()) return;

    const int imgW = input.width();
    const int imgH = input.height();
    const int gs   = qMax(2, params.gridSize);
    const int cols = (imgW + gs - 1) / gs;
    const int rows = (imgH + gs - 1) / gs;

    // Each row renders into its own QImage, then we composite them.
    // This avoids any locking on the painter.
    QVector<QImage> rowImages(rows);
    for (int r = 0; r < rows; ++r) {
        int cellY = r * gs;
        int cellH = qMin(gs, imgH - cellY);
        rowImages[r] = QImage(imgW, cellH, QImage::Format_ARGB32_Premultiplied);
        rowImages[r].fill(Qt::transparent);
    }

    // Build job list
    QVector<RowJob> jobs(rows);
    for (int r = 0; r < rows; ++r) {
        jobs[r] = { &input, &rowImages[r], &params, r, cols, gs };
    }

    // Run in parallel
    QtConcurrent::blockingMap(jobs, &HalftoneRenderer::renderRow);

    // Composite row images onto the output painter
    for (int r = 0; r < rows; ++r) {
        int cellY = r * gs;
        output.drawImage(0, cellY, rowImages[r]);
    }
}

// ---------------------------------------------------------------------------
// Per-row rendering (runs on thread pool threads)
// ---------------------------------------------------------------------------

void HalftoneRenderer::renderRow(const RowJob& job)
{
    const QImage&         input  = *job.input;
    QImage&               canvas = *job.canvas;
    const HalftoneParams& params = *job.params;

    const int gs    = job.gs;
    const int row   = job.row;
    const int cols  = job.totalCols;
    const int imgW  = input.width();
    const int imgH  = input.height();

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (int col = 0; col < cols; ++col) {
        int cellX = col * gs;
        int cellY = row * gs;
        int cellW = qMin(gs, imgW - cellX);
        int cellH = qMin(gs, imgH - cellY);

        // cx/cy in canvas-local coordinates (row strip starts at y=0)
        float cx = cellX + cellW * 0.5f;
        float cy = cellH * 0.5f;

        float lum      = sampleLuminosity(input, cellX, cellY, cellW, cellH);
        float baseR    = (gs * 0.5f) * params.symbolSize;
        float darkness = 1.0f - lum;
        float scale    = std::pow(darkness, 1.0f / qMax(0.01f, params.gamma));
        float r        = baseR * scale;

        if (r < 0.5f) continue;

        // Shape selection
        HalftoneShape shape   = params.shape;
        QString       svgPath = params.customSvgPath;

        if (params.multiSymbolEnabled) {
            int lumInt = static_cast<int>(lum * 255.f);
            shape   = params.symbolSlots[0].shape;
            svgPath = params.symbolSlots[0].svgPath;
            for (int s = 3; s >= 0; --s) {
                if (lumInt >= params.symbolSlots[s].threshold) {
                    shape   = params.symbolSlots[s].shape;
                    svgPath = params.symbolSlots[s].svgPath;
                    break;
                }
            }
        }

        // Fill color
        QColor fillColor = params.fillColor;
        if (params.useImageColors)
            fillColor = sampleAverageColor(input, cellX, cellY, cellW, cellH);
        fillColor.setAlphaF(params.opacity);

        // Jitter
        float rotationDeg = 0.0f;
        if (params.jitter > 0.0f) {
            std::mt19937 rng(cellSeed(col, row));
            std::uniform_real_distribution<float> dist(-180.0f, 180.0f);
            rotationDeg = dist(rng) * params.jitter;
        }

        painter.save();

        if (shape == HalftoneShape::CustomSVG && !svgPath.isEmpty()) {
            // SVG: render via QSvgRenderer
            QSvgRenderer svgRenderer(svgPath);
            if (svgRenderer.isValid()) {
                float size = r * 2.0f;
                QTransform t;
                t.translate(cx, cy);
                t.rotate(rotationDeg);
                t.translate(-size * 0.5f, -size * 0.5f);
                painter.setTransform(t);
                painter.setOpacity(params.opacity);
                svgRenderer.render(&painter, QRectF(0, 0, size, size));
            }
        } else {
            QPainterPath path = buildShape(shape, 0.0f, 0.0f, r);

            QTransform t;
            t.translate(cx, cy);
            t.rotate(rotationDeg);
            QPainterPath transformed = t.map(path);

            if (params.strokeEnabled) {
                QPen pen(params.strokeColor, params.strokeWidth);
                pen.setJoinStyle(Qt::RoundJoin);
                pen.setCapStyle(Qt::RoundCap);
                painter.setPen(pen);
            } else {
                painter.setPen(Qt::NoPen);
            }

            painter.setBrush(fillColor);
            painter.drawPath(transformed);
        }

        painter.restore();
    }
}

// ---------------------------------------------------------------------------
// Shape builders
// ---------------------------------------------------------------------------

QPainterPath HalftoneRenderer::buildCircle(float cx, float cy, float r)
{
    QPainterPath p;
    p.addEllipse(QPointF(cx, cy), r, r);
    return p;
}

QPainterPath HalftoneRenderer::buildSquare(float cx, float cy, float r)
{
    QPainterPath p;
    p.addRect(cx - r, cy - r, r * 2, r * 2);
    return p;
}

QPainterPath HalftoneRenderer::buildStar(float cx, float cy, float r, int points)
{
    QPainterPath p;
    float innerR = r * 0.4f;
    float step   = M_PI / points;
    for (int i = 0; i < points * 2; ++i) {
        float angle = i * step - M_PI_2;
        float rad   = (i % 2 == 0) ? r : innerR;
        float x     = cx + std::cos(angle) * rad;
        float y     = cy + std::sin(angle) * rad;
        if (i == 0) p.moveTo(x, y);
        else        p.lineTo(x, y);
    }
    p.closeSubpath();
    return p;
}

QPainterPath HalftoneRenderer::buildSpark(float cx, float cy, float r)
{
    QPainterPath p;
    float thin = r * 0.18f;
    p.moveTo(cx,        cy - r);
    p.lineTo(cx + thin, cy - thin);
    p.lineTo(cx + r,    cy);
    p.lineTo(cx + thin, cy + thin);
    p.lineTo(cx,        cy + r);
    p.lineTo(cx - thin, cy + thin);
    p.lineTo(cx - r,    cy);
    p.lineTo(cx - thin, cy - thin);
    p.closeSubpath();
    return p;
}

QPainterPath HalftoneRenderer::buildCrossX(float cx, float cy, float r)
{
    QPainterPath p;
    float w = r * 0.28f;
    p.moveTo(cx - r,     cy - r + w);
    p.lineTo(cx - r + w, cy - r);
    p.lineTo(cx,         cy - w);
    p.lineTo(cx + r - w, cy - r);
    p.lineTo(cx + r,     cy - r + w);
    p.lineTo(cx + w,     cy);
    p.lineTo(cx + r,     cy + r - w);
    p.lineTo(cx + r - w, cy + r);
    p.lineTo(cx,         cy + w);
    p.lineTo(cx - r + w, cy + r);
    p.lineTo(cx - r,     cy + r - w);
    p.lineTo(cx - w,     cy);
    p.closeSubpath();
    return p;
}

QPainterPath HalftoneRenderer::buildPlus(float cx, float cy, float r)
{
    float w = r * 0.28f;
    QPainterPath p;
    p.addRect(cx - r, cy - w, r * 2, w * 2);
    p.addRect(cx - w, cy - r, w * 2, r * 2);
    return p.simplified();
}

QPainterPath HalftoneRenderer::buildShape(HalftoneShape shape, float cx, float cy, float r)
{
    switch (shape) {
        case HalftoneShape::Circle:  return buildCircle(cx, cy, r);
        case HalftoneShape::Square:  return buildSquare(cx, cy, r);
        case HalftoneShape::Star:    return buildStar  (cx, cy, r);
        case HalftoneShape::Spark:   return buildSpark (cx, cy, r);
        case HalftoneShape::CrossX:  return buildCrossX(cx, cy, r);
        case HalftoneShape::Plus:    return buildPlus  (cx, cy, r);
        default:                     return buildCircle(cx, cy, r);
    }
}

// ---------------------------------------------------------------------------
// Sampling helpers
// ---------------------------------------------------------------------------

float HalftoneRenderer::sampleLuminosity(const QImage& img,
                                          int cellX, int cellY,
                                          int cellW, int cellH)
{
    double sum   = 0.0;
    int    count = 0;
    const QImage rgb = img.format() == QImage::Format_RGB32
                     ? img : img.convertToFormat(QImage::Format_RGB32);

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

QColor HalftoneRenderer::sampleAverageColor(const QImage& img,
                                             int cellX, int cellY,
                                             int cellW, int cellH)
{
    long long r = 0, g = 0, b = 0;
    int count = 0;
    const QImage rgb = img.format() == QImage::Format_RGB32
                     ? img : img.convertToFormat(QImage::Format_RGB32);

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
