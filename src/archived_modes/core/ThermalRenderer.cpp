#include "ThermalRenderer.h"
#include "ColorMath.h"
#include "Parallel.h"

#include <array>
#include <vector>

namespace {

// Classic FLIR-style "Iron" ramp: black → purple → red → orange → yellow → white.
struct IronStop { float t; int r, g, b; };
const std::array<IronStop, 7>& ironRamp()
{
    static const std::array<IronStop, 7> ramp = {{
        { 0.00f,   0,   0,   0 },
        { 0.15f,  40,   0,  90 },
        { 0.35f, 140,   0, 110 },
        { 0.55f, 220,  50,   0 },
        { 0.75f, 250, 150,   0 },
        { 0.90f, 255, 220,  50 },
        { 1.00f, 255, 255, 255 },
    }};
    return ramp;
}

QColor ironColor(float t)
{
    const auto& ramp = ironRamp();
    t = qBound(0.0f, t, 1.0f);
    for (size_t i = 1; i < ramp.size(); ++i) {
        if (t <= ramp[i].t) {
            const IronStop& a = ramp[i - 1];
            const IronStop& b = ramp[i];
            const float span = qMax(0.0001f, b.t - a.t);
            const float f = (t - a.t) / span;
            return QColor(qRound(a.r + (b.r - a.r) * f),
                          qRound(a.g + (b.g - a.g) * f),
                          qRound(a.b + (b.b - a.b) * f));
        }
    }
    return QColor(255, 255, 255);
}

// Cheap separable box blur over RGB, alpha untouched — same algorithm as
// ImageAdjuster::boxBlur (kept local: that one's private to ImageAdjuster).
void boxBlurRgb(QImage& img, int radius)
{
    if (radius < 1) return;
    const int w = img.width(), h = img.height();
    if (w < 3 || h < 3) return;
    img.detach();

    std::vector<quint32> tmp(size_t(w) * size_t(h));
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            int sr = 0, sg = 0, sb = 0, count = 0;
            for (int x = -radius; x <= radius; ++x) {
                const int xi = qBound(0, x, w - 1);
                const QRgb p = line[xi];
                sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
                ++count;
            }
            for (int x = 0; x < w; ++x) {
                tmp[size_t(y) * w + x] = qRgba(sr / count, sg / count, sb / count, qAlpha(line[x]));
                const int xAdd = qBound(0, x + radius + 1, w - 1);
                const int xSub = qBound(0, x - radius,     w - 1);
                const QRgb pa = line[xAdd], ps = line[xSub];
                sr += qRed(pa) - qRed(ps); sg += qGreen(pa) - qGreen(ps); sb += qBlue(pa) - qBlue(ps);
            }
        }
    });
    parallelRows(w, [&](int x0, int x1) {
        for (int x = x0; x < x1; ++x) {
            int sr = 0, sg = 0, sb = 0, count = 0;
            for (int y = -radius; y <= radius; ++y) {
                const quint32 p = tmp[size_t(qBound(0, y, h - 1)) * w + x];
                sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
                ++count;
            }
            for (int y = 0; y < h; ++y) {
                QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
                line[x] = qRgba(sr / count, sg / count, sb / count, qAlpha(line[x]));
                const int yAdd = qBound(0, y + radius + 1, h - 1);
                const int ySub = qBound(0, y - radius,     h - 1);
                const quint32 pa = tmp[size_t(yAdd) * w + x], ps = tmp[size_t(ySub) * w + x];
                sr += qRed(pa) - qRed(ps); sg += qGreen(pa) - qGreen(ps); sb += qBlue(pa) - qBlue(ps);
            }
        }
    });
}

} // namespace

bool ThermalRenderer::gpuRenderable(const ThermalSettings& s)
{
    return s.tonal.enabled && s.tonal.mode != ToneMode::Palette;
}

QImage ThermalRenderer::render(const QImage& input, const ThermalSettings& params)
{
    if (input.isNull()) return input;
    QImage src = input.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    if (w < 1 || h < 1) return src;

    const int blurRadius = qRound(qBound(0.0f, params.blur, 20.0f));
    if (blurRadius > 0) boxBlurRgb(src, blurRadius);

    QImage out(w, h, QImage::Format_ARGB32);
    out.detach();

    const float gain = qBound(0.1f, params.gain, 3.0f);
    const float bias = qBound(-1.0f, params.bias, 1.0f);
    const float opacity = qBound(0.0f, params.opacity, 1.0f);

    const TonalSettings& tonal = params.tonal;
    const bool imageColors = (tonal.mode == ToneMode::ImageColors) || tonal.tones.empty();

    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* srcLine = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            QRgb* dstLine = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb p = srcLine[x];
                const float lum = ColorMath::perceptualLuma(p);
                const float t = qBound(0.0f, (lum - 0.5f) * gain + 0.5f + bias, 1.0f);

                QColor fill;
                if (imageColors) {
                    fill = ironColor(t);
                } else {
                    const int idx = pickToneIndex(tonal.tones, t);
                    const ToneEntry& te = tonal.tones[size_t(idx)];
                    fill = te.color;
                    fill.setAlphaF(qBound(0.0f, te.opacity, 1.0f));
                }

                const int a = qBound(0, qRound(qAlpha(p) * fill.alphaF() * opacity), 255);
                dstLine[x] = qRgba(fill.red(), fill.green(), fill.blue(), a);
            }
        }
    });

    return out;
}
