#include "MetalEmbossRenderer.h"
#include "ColorMath.h"
#include "Parallel.h"

#include <QtMath>
#include <cmath>
#include <vector>

namespace {

float lumaAt(const QImage& src, int x, int y, int w, int h)
{
    x = qBound(0, x, w - 1);
    y = qBound(0, y, h - 1);
    const QRgb p = reinterpret_cast<const QRgb*>(src.constScanLine(y))[x];
    return ColorMath::perceptualLuma(p);
}

} // namespace

bool MetalEmbossRenderer::gpuRenderable(const MetalEmbossSettings& s)
{
    return s.tonal.enabled && s.tonal.mode != ToneMode::Palette;
}

QImage MetalEmbossRenderer::render(const QImage& input, const MetalEmbossSettings& params)
{
    if (input.isNull()) return input;
    const QImage src = input.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    if (w < 3 || h < 3) return src;

    const float depth   = qBound(0.0f, params.depth, 100.0f) / 100.0f * 4.0f;   // gradient scale
    const float specAmt = qBound(0.0f, params.specular, 100.0f) / 100.0f;
    const float opacity = qBound(0.0f, params.opacity, 1.0f);
    const QColor metal  = params.metalColor;

    // Light direction from angle (azimuth) + altitude (elevation).
    const double azRad = qDegreesToRadians(double(params.angle));
    const double elRad = qDegreesToRadians(double(qBound(10.0f, params.altitude, 90.0f)));
    const float lx = float(std::cos(elRad) * std::cos(azRad));
    const float ly = float(std::cos(elRad) * std::sin(azRad));
    const float lz = float(std::sin(elRad));

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
            const QRgb* srcLine = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            QRgb* dstLine = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                // Sobel gradient of luminance = surface slope.
                const float tl = lumaAt(src, x - 1, y - 1, w, h), t = lumaAt(src, x, y - 1, w, h), tr = lumaAt(src, x + 1, y - 1, w, h);
                const float l  = lumaAt(src, x - 1, y,     w, h),                                   r  = lumaAt(src, x + 1, y,     w, h);
                const float bl = lumaAt(src, x - 1, y + 1, w, h), b = lumaAt(src, x, y + 1, w, h), br = lumaAt(src, x + 1, y + 1, w, h);
                const float gx = ((tr + 2.0f * r + br) - (tl + 2.0f * l + bl)) * depth;
                const float gy = ((bl + 2.0f * b + br) - (tl + 2.0f * t + tr)) * depth;

                // Surface normal from the height-field gradient, normalized.
                float nx = -gx, ny = -gy, nz = 1.0f;
                const float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
                nx *= invLen; ny *= invLen; nz *= invLen;

                const float diffuse = qMax(0.0f, nx * lx + ny * ly + nz * lz);

                // Blinn-Phong half-vector specular, view = straight at the surface.
                float hx = lx, hy = ly, hz = lz + 1.0f;
                const float hLen = 1.0f / std::sqrt(hx * hx + hy * hy + hz * hz);
                hx *= hLen; hy *= hLen; hz *= hLen;
                const float ndoth = qMax(0.0f, nx * hx + ny * hy + nz * hz);
                const float spec = std::pow(ndoth, 24.0f) * specAmt;

                QColor fill;
                if (paletteMode) {
                    const float rLin = diffuse, gLin = diffuse, bLin = diffuse;
                    const int idx = ColorMath::nearestPaletteIndex(
                        palette, ColorMath::linearToOklab(rLin, gLin, bLin));
                    fill = QColor::fromRgba(palette[size_t(idx)].out);
                } else if (imageColors) {
                    fill = QColor(qBound(0, qRound(metal.red()   * diffuse + 255.0f * spec), 255),
                                  qBound(0, qRound(metal.green() * diffuse + 255.0f * spec), 255),
                                  qBound(0, qRound(metal.blue()  * diffuse + 255.0f * spec), 255));
                } else {
                    const int idx = pickToneIndex(tonal.tones, diffuse);
                    const ToneEntry& te = tonal.tones[size_t(idx)];
                    fill = te.color;
                    fill.setAlphaF(qBound(0.0f, te.opacity, 1.0f));
                    // Specular still lifts toward white even on a Fill tone.
                    fill = QColor(qBound(0, int(fill.red()   + (255 - fill.red())   * spec), 255),
                                  qBound(0, int(fill.green() + (255 - fill.green()) * spec), 255),
                                  qBound(0, int(fill.blue()  + (255 - fill.blue())  * spec), 255),
                                  fill.alpha());
                }

                const int a = qBound(0, qRound(qAlpha(srcLine[x]) * fill.alphaF() * opacity), 255);
                dstLine[x] = qRgba(fill.red(), fill.green(), fill.blue(), a);
            }
        }
    });

    return out;
}
