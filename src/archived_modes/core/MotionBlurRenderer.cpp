#include "MotionBlurRenderer.h"
#include "ColorMath.h"
#include "Parallel.h"

#include <QtMath>
#include <cmath>
#include <vector>

namespace {

struct LinPixel { float r, g, b, a; };

// Bilinear sample at (x, y) in linear light, clamped to the image edges.
LinPixel sampleBilinear(const QImage& src, float x, float y, int w, int h)
{
    x = qBound(0.0f, x, float(w - 1));
    y = qBound(0.0f, y, float(h - 1));
    const int x0 = int(x), y0 = int(y);
    const int x1 = qMin(x0 + 1, w - 1), y1 = qMin(y0 + 1, h - 1);
    const float fx = x - float(x0), fy = y - float(y0);

    const QRgb* row0 = reinterpret_cast<const QRgb*>(src.constScanLine(y0));
    const QRgb* row1 = reinterpret_cast<const QRgb*>(src.constScanLine(y1));
    const QRgb p00 = row0[x0], p10 = row0[x1], p01 = row1[x0], p11 = row1[x1];
    const auto& lut = ColorMath::srgbToLinearLUT();

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    const float r0 = lerp(lut[size_t(qRed(p00))],   lut[size_t(qRed(p10))],   fx);
    const float r1 = lerp(lut[size_t(qRed(p01))],   lut[size_t(qRed(p11))],   fx);
    const float g0 = lerp(lut[size_t(qGreen(p00))], lut[size_t(qGreen(p10))], fx);
    const float g1 = lerp(lut[size_t(qGreen(p01))], lut[size_t(qGreen(p11))], fx);
    const float b0 = lerp(lut[size_t(qBlue(p00))],  lut[size_t(qBlue(p10))],  fx);
    const float b1 = lerp(lut[size_t(qBlue(p01))],  lut[size_t(qBlue(p11))],  fx);
    const float a0 = lerp(float(qAlpha(p00)), float(qAlpha(p10)), fx);
    const float a1 = lerp(float(qAlpha(p01)), float(qAlpha(p11)), fx);

    return { lerp(r0, r1, fy), lerp(g0, g1, fy), lerp(b0, b1, fy), lerp(a0, a1, fy) };
}

} // namespace

QImage MotionBlurRenderer::render(const QImage& input, const MotionBlurSettings& params)
{
    if (input.isNull()) return input;
    const QImage src = input.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    if (w < 1 || h < 1) return src;

    QImage out(w, h, QImage::Format_ARGB32);
    out.detach();   // no COW race inside the parallel loop below (see ImageAdjuster)

    const float distance = qBound(0.0f, params.distance, 200.0f);
    const double rad = qDegreesToRadians(double(params.angle));
    const float dx = float(std::cos(rad)), dy = float(std::sin(rad));
    const int samples = qBound(1, qRound(distance) + 1, 96);
    const float opacity = qBound(0.0f, params.opacity, 1.0f);

    const TonalSettings& tonal = params.tonal;
    const bool imageColors = (tonal.mode == ToneMode::ImageColors) || tonal.tones.empty();
    const bool paletteMode = (tonal.mode == ToneMode::Palette) && !tonal.tones.empty();
    const std::vector<ColorMath::PaletteEntry> palette =
        paletteMode ? ColorMath::buildPalette(tonal.tones)
                    : std::vector<ColorMath::PaletteEntry>{};

    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                float rSum = 0, gSum = 0, bSum = 0, aSum = 0;
                for (int i = 0; i < samples; ++i) {
                    const float t = samples > 1
                        ? (float(i) / float(samples - 1) - 0.5f) * distance
                        : 0.0f;
                    const LinPixel p = sampleBilinear(src, float(x) + t * dx, float(y) + t * dy, w, h);
                    rSum += p.r; gSum += p.g; bSum += p.b; aSum += p.a;
                }
                const float rLin = rSum / samples, gLin = gSum / samples, bLin = bSum / samples;
                const float aAvg = aSum / samples;

                QColor fill;
                if (imageColors) {
                    fill = QColor(ColorMath::linearToSrgb8(rLin),
                                  ColorMath::linearToSrgb8(gLin),
                                  ColorMath::linearToSrgb8(bLin));
                } else if (paletteMode) {
                    const int idx = ColorMath::nearestPaletteIndex(
                        palette, ColorMath::linearToOklab(rLin, gLin, bLin));
                    fill = QColor::fromRgba(palette[size_t(idx)].out);
                } else {
                    const float lum = ColorMath::perceptualLumaFromLinear(rLin, gLin, bLin);
                    const int idx = pickToneIndex(tonal.tones, lum);
                    const ToneEntry& te = tonal.tones[size_t(idx)];
                    fill = te.color;
                    fill.setAlphaF(qBound(0.0f, te.opacity, 1.0f));
                }

                const int a = qBound(0, qRound(aAvg * fill.alphaF() * opacity), 255);
                line[x] = qRgba(fill.red(), fill.green(), fill.blue(), a);
            }
        }
    });

    return out;
}
