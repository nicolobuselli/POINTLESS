#include "AsciiRenderer.h"

#include <QFont>
#include <QtMath>
#include <cmath>

namespace {

float cellLuminosity(const QImage& rgb, int cx, int cy, int cw, int ch)
{
    double sum = 0.0;
    int count = 0;
    for (int y = cy; y < cy + ch; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = cx; x < cx + cw; ++x) {
            QRgb p = line[x];
            sum += 0.2126 * qRed(p) + 0.7152 * qGreen(p) + 0.0722 * qBlue(p);
            ++count;
        }
    }
    return count == 0 ? 1.0f : float(sum / (count * 255.0));
}

QColor cellAverageColor(const QImage& rgb, int cx, int cy, int cw, int ch)
{
    long long r = 0, g = 0, b = 0;
    int count = 0;
    for (int y = cy; y < cy + ch; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = cx; x < cx + cw; ++x) {
            QRgb p = line[x];
            r += qRed(p); g += qGreen(p); b += qBlue(p);
            ++count;
        }
    }
    return count == 0 ? QColor(Qt::black) : QColor(int(r / count), int(g / count), int(b / count));
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
    const int cellW = qMax(2, qRound(cellH * 0.55f));
    const int cols  = (imgW + cellW - 1) / cellW;
    const int rows  = (imgH + cellH - 1) / cellH;

    const bool  imageColors = (params.tonal.mode == ToneMode::ImageColors);
    const auto& tones       = params.tonal.tones;
    const float invGamma    = 1.0f / qMax(0.01f, params.gamma);

    QFont font("Consolas");
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(cellH);
    font.setWeight(QFont::DemiBold);

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

            const float lum = cellLuminosity(rgb, cx, cy, cw, ch);
            float darkness  = params.invert ? lum : 1.0f - lum;
            darkness        = std::pow(qBound(0.0f, darkness, 1.0f), invGamma);

            const int idx = qBound(0, qRound(darkness * (nChars - 1)), nChars - 1);
            const QChar c = charset.at(idx);
            if (c == QChar(' ')) continue;   // blank cell → background

            QColor pen;
            if (imageColors || tones.empty())
                pen = cellAverageColor(rgb, cx, cy, cw, ch);
            else
                pen = tones[pickToneIndex(tones, lum)].color;

            output.setPen(pen);
            output.drawText(QRectF(cx, cy, cellW, cellH),
                            Qt::AlignCenter | Qt::TextDontClip, QString(c));
        }
    }

    output.restore();
}
