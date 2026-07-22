#include "BlueprintRenderer.h"
#include "ColorMath.h"
#include "Parallel.h"

#include <cmath>
#include <vector>

namespace {

float lumaAt(const QImage& src, int x, int y, int w, int h)
{
    x = qBound(0, x, w - 1);
    y = qBound(0, y, h - 1);
    const QRgb p = reinterpret_cast<const QRgb*>(src.constScanLine(y))[x];
    return ColorMath::perceptualLuma(p) * 255.0f;
}

} // namespace

bool BlueprintRenderer::gpuRenderable(const BlueprintSettings& s)
{
    return s.tonal.enabled && s.tonal.mode != ToneMode::Palette;
}

QImage BlueprintRenderer::render(const QImage& input, const BlueprintSettings& params)
{
    if (input.isNull()) return input;
    const QImage src = input.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    if (w < 3 || h < 3) return src;

    // Sobel gradient magnitude over perceptual luma.
    std::vector<float> mag(size_t(w) * size_t(h));
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                const float tl = lumaAt(src, x - 1, y - 1, w, h), t = lumaAt(src, x, y - 1, w, h), tr = lumaAt(src, x + 1, y - 1, w, h);
                const float l  = lumaAt(src, x - 1, y,     w, h),                                   r  = lumaAt(src, x + 1, y,     w, h);
                const float bl = lumaAt(src, x - 1, y + 1, w, h), b = lumaAt(src, x, y + 1, w, h), br = lumaAt(src, x + 1, y + 1, w, h);
                const float gx = (tr + 2.0f * r + br) - (tl + 2.0f * l + bl);
                const float gy = (bl + 2.0f * b + br) - (tl + 2.0f * t + tr);
                mag[size_t(y) * w + x] = std::sqrt(gx * gx + gy * gy);
            }
        }
    });

    const int dilate = qBound(0, qRound(qBound(1.0f, params.lineWidth, 5.0f)) - 1, 4);
    const float threshold = qBound(0.0f, params.threshold, 255.0f);
    const float opacity = qBound(0.0f, params.opacity, 1.0f);
    const int alpha = qBound(0, qRound(opacity * 255.0f), 255);
    const QColor paper = params.paperColor;

    const TonalSettings& tonal = params.tonal;
    const bool imageColors = (tonal.mode == ToneMode::ImageColors) || tonal.tones.empty();
    const bool paletteMode = (tonal.mode == ToneMode::Palette) && !tonal.tones.empty();
    const std::vector<ColorMath::PaletteEntry> palette =
        paletteMode ? ColorMath::buildPalette(tonal.tones)
                    : std::vector<ColorMath::PaletteEntry>{};

    QImage out(w, h, QImage::Format_ARGB32);
    out.detach();

    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
            const QRgb* srcLine = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                float m = mag[size_t(y) * w + x];
                if (dilate > 0) {
                    for (int dy = -dilate; dy <= dilate; ++dy)
                        for (int dx = -dilate; dx <= dilate; ++dx) {
                            const int xi = qBound(0, x + dx, w - 1);
                            const int yi = qBound(0, y + dy, h - 1);
                            m = qMax(m, mag[size_t(yi) * w + xi]);
                        }
                }

                QColor fill;
                if (m > threshold) {
                    const QRgb sp = srcLine[x];
                    if (imageColors) {
                        fill = QColor(sp);
                    } else if (paletteMode) {
                        const float rLin = ColorMath::srgbToLinear(qRed(sp));
                        const float gLin = ColorMath::srgbToLinear(qGreen(sp));
                        const float bLin = ColorMath::srgbToLinear(qBlue(sp));
                        const int idx = ColorMath::nearestPaletteIndex(
                            palette, ColorMath::linearToOklab(rLin, gLin, bLin));
                        fill = QColor::fromRgba(palette[size_t(idx)].out);
                    } else {
                        const int idx = pickToneIndex(tonal.tones, qBound(0.0f, m / 255.0f, 1.0f));
                        const ToneEntry& te = tonal.tones[size_t(idx)];
                        fill = te.color;
                        fill.setAlphaF(qBound(0.0f, te.opacity, 1.0f));
                    }
                } else {
                    fill = paper;
                }

                const int a = qBound(0, qRound(fill.alphaF() * alpha), 255);
                line[x] = qRgba(fill.red(), fill.green(), fill.blue(), a);
            }
        }
    });

    return out;
}
