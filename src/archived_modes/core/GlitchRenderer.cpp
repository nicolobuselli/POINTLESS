#include "GlitchRenderer.h"
#include "ColorMath.h"
#include "Parallel.h"

#include <algorithm>
#include <vector>

namespace {

quint32 hashInt(quint32 x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline float lumaOf(QRgb p) { return ColorMath::perceptualLuma(p) * 255.0f; }

QImage renderGlitch(const QImage& src, const GlitchSettings& params)
{
    const int w = src.width(), h = src.height();
    const int blockSize = qBound(4, params.blockSize, 128);
    const float amount = qBound(0.0f, params.amount, 100.0f) / 100.0f;
    const int channelShift = qRound(qBound(0.0f, params.channelShift, 40.0f));
    const quint32 seed = quint32(params.seed) * 2654435761u;

    // 1. Per-band horizontal wrap-shift: each band is "affected" with
    // probability = amount, displaced by up to ±15% of the frame width.
    QImage shifted(w, h, QImage::Format_ARGB32);
    shifted.detach();
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const int band = y / blockSize;
            const quint32 hv = hashInt(seed ^ quint32(band) * 0x9e3779b9u);
            const float r1 = float(hv & 0xFFFF) / 65535.0f;
            const float r2 = float((hv >> 16) & 0xFFFF) / 65535.0f;
            int dx = 0;
            if (r1 < amount)
                dx = qRound((r2 * 2.0f - 1.0f) * amount * float(w) * 0.15f);

            const QRgb* srcLine = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            QRgb* dstLine = reinterpret_cast<QRgb*>(shifted.scanLine(y));
            for (int x = 0; x < w; ++x)
                dstLine[x] = srcLine[((x - dx) % w + w) % w];
        }
    });

    // 2. RGB channel split (chromatic aberration).
    QImage out(w, h, QImage::Format_ARGB32);
    out.detach();
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(shifted.constScanLine(y));
            QRgb* dstLine = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int xr = qBound(0, x - channelShift, w - 1);
                const int xb = qBound(0, x + channelShift, w - 1);
                const QRgb pr = line[xr], pg = line[x], pb = line[xb];
                dstLine[x] = qRgba(qRed(pr), qGreen(pg), qBlue(pb), qAlpha(pg));
            }
        }
    });
    return out;
}

// Real datamoshing: macroblock corruption. Each block has a chance to be
// replaced by a motion-compensated but WRONG source block (a random offset
// within ±displacement), the classic "frozen/smeared block" artifact of a
// P-frame decoded against the wrong reference.
QImage renderMacroblockCorruption(const QImage& src, const GlitchSettings& params)
{
    const int w = src.width(), h = src.height();
    const int bs = qBound(4, params.dmBlockSize, 64);
    const float amount = qBound(0.0f, params.dmAmount, 100.0f) / 100.0f;
    const int maxDisp = qRound(qBound(0.0f, params.dmDisplacement, 100.0f));
    const quint32 seed = quint32(params.dmSeed) * 2654435761u;

    QImage out = src;
    out.detach();

    const int cols = (w + bs - 1) / bs;
    const int rows = (h + bs - 1) / bs;

    for (int by = 0; by < rows; ++by) {
        for (int bx = 0; bx < cols; ++bx) {
            const quint32 hv = hashInt(seed ^ quint32(bx) * 0x9e3779b9u ^ quint32(by) * 0x85ebca6bu);
            const float r0 = float(hv & 0xFFu) / 255.0f;
            if (r0 >= amount) continue;   // this block stays clean

            const float rx = float((hv >> 8)  & 0xFFu) / 255.0f * 2.0f - 1.0f;
            const float ry = float((hv >> 16) & 0xFFu) / 255.0f * 2.0f - 1.0f;
            const int dx = qRound(rx * maxDisp);
            const int dy = qRound(ry * maxDisp);
            if (dx == 0 && dy == 0) continue;

            const int x0 = bx * bs, y0 = by * bs;
            const int x1 = qMin(w, x0 + bs), y1 = qMin(h, y0 + bs);
            // Copy row-by-row from the (wrapped) displaced source block —
            // read from `src` (untouched) so overlapping blocks in the same
            // pass never read already-corrupted pixels.
            for (int y = y0; y < y1; ++y) {
                const int sy = ((y + dy) % h + h) % h;
                const QRgb* srcLine = reinterpret_cast<const QRgb*>(src.constScanLine(sy));
                QRgb* dstLine = reinterpret_cast<QRgb*>(out.scanLine(y));
                for (int x = x0; x < x1; ++x) {
                    const int sx = ((x + dx) % w + w) % w;
                    dstLine[x] = srcLine[sx];
                }
            }
        }
    }
    return out;
}

QImage renderPixelSort(const QImage& src, const GlitchSettings& params)
{
    QImage out = src;
    out.detach();   // no COW race inside the parallel loops below
    const int w = out.width(), h = out.height();
    const float threshold = qBound(0.0f, params.sortThreshold, 255.0f);

    auto sortRun = [](QRgb* begin, QRgb* end) {
        std::sort(begin, end, [](QRgb a, QRgb b) { return lumaOf(a) < lumaOf(b); });
    };

    if (!params.sortVertical) {
        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
                int x = 0;
                while (x < w) {
                    if (lumaOf(line[x]) <= threshold) { ++x; continue; }
                    int runStart = x;
                    while (x < w && lumaOf(line[x]) > threshold) ++x;
                    sortRun(line + runStart, line + x);
                }
            }
        });
    } else {
        const size_t hh = size_t(h);
        parallelRows(w, [&](int x0, int x1) {
            std::vector<QRgb> col(hh);
            for (int x = x0; x < x1; ++x) {
                for (int y = 0; y < h; ++y)
                    col[size_t(y)] = reinterpret_cast<QRgb*>(out.scanLine(y))[x];
                int y = 0;
                while (y < h) {
                    if (lumaOf(col[size_t(y)]) <= threshold) { ++y; continue; }
                    int runStart = y;
                    while (y < h && lumaOf(col[size_t(y)]) > threshold) ++y;
                    sortRun(&col[size_t(runStart)], &col[size_t(y)]);
                }
                for (int y2 = 0; y2 < h; ++y2)
                    reinterpret_cast<QRgb*>(out.scanLine(y2))[x] = col[size_t(y2)];
            }
        });
    }
    return out;
}

QImage renderCrt(const QImage& src, const GlitchSettings& params)
{
    QImage out = src;
    out.detach();
    const int w = out.width(), h = out.height();
    const int spacing = qBound(1, params.scanlineSpacing, 8);
    const float intensity = qBound(0.0f, params.scanlineIntensity, 100.0f) / 100.0f;
    const float mask = qBound(0.0f, params.maskStrength, 100.0f) / 100.0f;

    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
            const bool darkBand = (y % (spacing * 2)) >= spacing;
            const float lineFactor = darkBand ? (1.0f - intensity) : 1.0f;
            for (int x = 0; x < w; ++x) {
                const QRgb p = line[x];
                float mr = 1.0f, mg = 1.0f, mb = 1.0f;
                switch (x % 3) {
                    case 0: mg = 1.0f - mask; mb = 1.0f - mask; break;
                    case 1: mr = 1.0f - mask; mb = 1.0f - mask; break;
                    default: mr = 1.0f - mask; mg = 1.0f - mask; break;
                }
                const int r = qBound(0, qRound(qRed(p)   * lineFactor * mr), 255);
                const int g = qBound(0, qRound(qGreen(p) * lineFactor * mg), 255);
                const int b = qBound(0, qRound(qBlue(p)  * lineFactor * mb), 255);
                line[x] = qRgba(r, g, b, qAlpha(p));
            }
        }
    });
    return out;
}

} // namespace

bool GlitchRenderer::gpuRenderable(const GlitchSettings& s)
{
    return s.algorithm != GlitchAlgorithm::PixelSort
        && s.algorithm != GlitchAlgorithm::Datamosh
        && s.tonal.enabled && s.tonal.mode != ToneMode::Palette;
}

QImage GlitchRenderer::render(const QImage& input, const GlitchSettings& params)
{
    if (input.isNull()) return input;
    const QImage src = input.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    if (w < 1 || h < 1) return src;

    QImage out;
    switch (params.algorithm) {
        case GlitchAlgorithm::PixelSort: out = renderPixelSort(src, params); break;
        case GlitchAlgorithm::Crt:       out = renderCrt(src, params);       break;
        case GlitchAlgorithm::Datamosh:  out = renderMacroblockCorruption(src, params); break;
        default:                         out = renderGlitch(src, params);   break;
    }

    // Shared Fill: ImageColors = passthrough; FixedTones/Palette recolor,
    // same rule as Motion Blur/Thermal's simple (non-diffused) path.
    const TonalSettings& tonal = params.tonal;
    const bool imageColors = (tonal.mode == ToneMode::ImageColors) || tonal.tones.empty();
    if (!imageColors) {
        const bool paletteMode = (tonal.mode == ToneMode::Palette) && !tonal.tones.empty();
        const std::vector<ColorMath::PaletteEntry> palette =
            paletteMode ? ColorMath::buildPalette(tonal.tones)
                        : std::vector<ColorMath::PaletteEntry>{};

        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const QRgb p = line[x];
                    QColor fill;
                    if (paletteMode) {
                        const float rLin = ColorMath::srgbToLinear(qRed(p));
                        const float gLin = ColorMath::srgbToLinear(qGreen(p));
                        const float bLin = ColorMath::srgbToLinear(qBlue(p));
                        const int idx = ColorMath::nearestPaletteIndex(
                            palette, ColorMath::linearToOklab(rLin, gLin, bLin));
                        fill = QColor::fromRgba(palette[size_t(idx)].out);
                    } else {
                        const int idx = pickToneIndex(tonal.tones, ColorMath::perceptualLuma(p));
                        const ToneEntry& te = tonal.tones[size_t(idx)];
                        fill = te.color;
                        fill.setAlphaF(qBound(0.0f, te.opacity, 1.0f));
                    }
                    const int a = qBound(0, qRound(qAlpha(p) * fill.alphaF()), 255);
                    line[x] = qRgba(fill.red(), fill.green(), fill.blue(), a);
                }
            }
        });
    }

    const float opacity = qBound(0.0f, params.opacity, 1.0f);
    if (opacity < 0.999f) {
        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const QRgb p = line[x];
                    line[x] = qRgba(qRed(p), qGreen(p), qBlue(p),
                                    qRound(qAlpha(p) * opacity));
                }
            }
        });
    }

    return out;
}
