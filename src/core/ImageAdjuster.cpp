#include "ImageAdjuster.h"
#include "Parallel.h"

#include <QtMath>
#include <cstring>
#include <vector>

namespace {

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

inline quint32 hash2d(quint32 x, quint32 y)
{
    quint32 h = x * 73856093u ^ y * 19349663u;
    h ^= (h >> 16);
    h *= 0x45d9f3bu;
    h ^= (h >> 16);
    return h;
}

// Parallel per-pixel LUT on RGB, alpha preserved (shared by the point ops).
void applyLut(QImage& img, const uint8_t* lut)
{
    // If img still shares data (convertToFormat on an already-ARGB32 source is
    // a shallow copy), the non-const scanLine() below would copy-on-write
    // *inside* the parallel loop: N threads racing detach() = heap corruption.
    // Detach once here, on the calling thread. Same guard in every mutator below.
    img.detach();
    const int w = img.width(), h = img.height();
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb p = line[x];
                line[x] = qRgba(lut[qRed(p)], lut[qGreen(p)], lut[qBlue(p)], qAlpha(p));
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Point ops as composable 256-entry tables.
//
// Every consecutive run of point ops (brightness/contrast, gamma, levels,
// invert, posterize) collapses into ONE table and ONE full-image pass instead
// of a pass each. Composition quantises intermediates to 8 bits — exactly what
// the old chain did anyway, since each op wrote its result back into the 8-bit
// image before the next one read it, so the output is bit-identical.
// ---------------------------------------------------------------------------

struct LutChain {
    uint8_t t[256];
    bool    identity = true;

    LutChain() { reset(); }
    void reset() { for (int i = 0; i < 256; ++i) t[i] = uint8_t(i); }

    // t := next ∘ t
    void compose(const uint8_t* next)
    {
        for (int i = 0; i < 256; ++i) t[i] = next[t[i]];
        identity = false;
    }

    // Neighbourhood ops (blur/edge/sharpen/grain) read real pixels, so the
    // pending table has to land before they run.
    void flush(QImage& img)
    {
        if (identity) return;
        applyLut(img, t);
        reset();
        identity = true;
    }
};

void brightnessContrastLut(uint8_t lut[256], int brightness, int contrast)
{
    const float c = contrast * 2.55f;
    const float factor = (259.0f * (c + 255.0f)) / (255.0f * (259.0f - c));
    const float offset = brightness * 1.275f;
    for (int i = 0; i < 256; ++i)
        lut[i] = uint8_t(clamp255(qRound(factor * (i - 128) + 128 + offset)));
}

void gammaLut(uint8_t lut[256], float gamma)
{
    if (gamma <= 0.0f) gamma = 0.01f;
    for (int i = 0; i < 256; ++i)
        lut[i] = uint8_t(clamp255(qRound(qPow(i / 255.0, gamma) * 255.0)));
}

void levelsLut(uint8_t lut[256], int blackPoint, float midPoint, int whitePoint)
{
    const float range = float(whitePoint - blackPoint);
    if (midPoint <= 0.0f) midPoint = 0.01f;
    for (int i = 0; i < 256; ++i) {
        float v = (range > 0.0f) ? (i - blackPoint) / range : 0.0f;
        v = qBound(0.0f, v, 1.0f);
        // midPoint > 1 brightens, < 1 darkens — same convention as Photoshop Levels
        if (midPoint != 1.0f)
            v = qPow(v, 1.0f / midPoint);
        lut[i] = uint8_t(clamp255(qRound(v * 255.0f)));
    }
}

void invertLut(uint8_t lut[256])
{
    for (int i = 0; i < 256; ++i) lut[i] = uint8_t(255 - i);
}

void posterizeLut(uint8_t lut[256], int levels)
{
    levels = qMax(2, levels);
    for (int i = 0; i < 256; ++i) {
        int step = qRound(float(i) / 255.0f * (levels - 1));
        lut[i] = uint8_t(clamp255(qRound(float(step) / float(levels - 1) * 255.0f)));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry point
// Pipeline: resize → brightness → contrast → gamma → levels → saturation →
//           invert → blur → edge enhancement → sharpen → grain → posterize →
//           threshold
// ---------------------------------------------------------------------------

QImage ImageAdjuster::apply(const QImage& src, const Adjustments& a)
{
    if (src.isNull()) return src;

    QImage img = src.convertToFormat(QImage::Format_ARGB32);

    // 1. Resize
    if (a.sizePct != 100) {
        int w = qMax(8, img.width()  * a.sizePct / 100);
        int h = qMax(8, img.height() * a.sizePct / 100);
        img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    // Steps 2-4, 6 and 11 are point ops: they accumulate into `chain` and are
    // paid for in a single pass at the next flush, not one pass each.
    LutChain chain;
    uint8_t  lut[256];

    // 2. Brightness / Contrast
    if (a.brightness != 0 || a.contrast != 0) {
        brightnessContrastLut(lut, a.brightness, a.contrast);
        chain.compose(lut);
    }

    // 3. Gamma
    if (a.gamma != 100) {
        gammaLut(lut, a.gamma / 100.0f);
        chain.compose(lut);
    }

    // 4. Levels
    if (a.levelsBlack != 0 || a.levelsMid != 100 || a.levelsWhite != 255) {
        levelsLut(lut, a.levelsBlack, a.levelsMid / 100.0f, a.levelsWhite);
        chain.compose(lut);
    }

    // 5. Saturation — cross-channel, breaks the chain.
    if (a.saturation != 0) {
        chain.flush(img);
        saturate(img, a.saturation);
    }

    // 6. Invert
    if (a.invert) {
        invertLut(lut);
        chain.compose(lut);
    }

    // Steps 7-10 read neighbouring / real pixel values: land the chain first.
    if (a.blur > 0 || a.edgeEnhancement > 0 || a.sharpenStrength > 0 || a.grain > 0)
        chain.flush(img);

    // 7. Blur
    if (a.blur > 0) {
        int radius = qMax(1, qRound(a.blur * 0.35f));
        int passes = 2 + a.blur / 35;
        for (int i = 0; i < passes; ++i)
            boxBlur(img, radius);
    }

    // 8. Edge Enhancement
    if (a.edgeEnhancement > 0)
        edgeEnhance(img, a.edgeEnhancement);

    // 9. Sharpen (unsharp mask)
    if (a.sharpenStrength > 0)
        unsharpMask(img, a.sharpenStrength, a.sharpenRadius);

    // 10. Grain
    if (a.grain > 0)
        addGrain(img, a.grain);

    // 11. Posterize
    if (a.posterize < 256) {
        posterizeLut(lut, a.posterize);
        chain.compose(lut);
    }

    chain.flush(img);

    // 12. Threshold — luma-based, not a per-channel table.
    if (a.threshold > 0)
        applyThreshold(img, a.threshold);

    return img;
}

// ---------------------------------------------------------------------------
// Separable box blur (RGB, alpha preserved)
// ---------------------------------------------------------------------------

void ImageAdjuster::boxBlur(QImage& img, int radius)
{
    if (radius < 1) return;
    const int w = img.width();
    const int h = img.height();
    if (w < 3 || h < 3) return;
    img.detach();   // see applyLut: no COW inside parallel loops

    std::vector<quint32> tmp(size_t(w) * size_t(h));

    // Horizontal pass: img → tmp
    parallelRows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        int sr = 0, sg = 0, sb = 0;
        int count = 0;
        for (int x = -radius; x <= radius; ++x) {
            int xi = qBound(0, x, w - 1);
            QRgb p = line[xi];
            sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
            ++count;
        }
        for (int x = 0; x < w; ++x) {
            tmp[size_t(y) * w + x] = qRgba(sr / count, sg / count, sb / count,
                                           qAlpha(line[x]));
            int xAdd = qBound(0, x + radius + 1, w - 1);
            int xSub = qBound(0, x - radius,     w - 1);
            QRgb pa = line[xAdd], ps = line[xSub];
            sr += qRed(pa) - qRed(ps);
            sg += qGreen(pa) - qGreen(ps);
            sb += qBlue(pa) - qBlue(ps);
        }
    }
    });

    // Vertical pass: tmp → img (columns are independent).
    // The row base is computed from bits()/bytesPerLine() once instead of
    // calling scanLine(y) per pixel: the non-const overload re-checks detach()
    // on every call, and this loop runs w*h times.
    uchar* const  base = img.bits();
    const qsizetype bpl = img.bytesPerLine();
    parallelRows(w, [&](int x0, int x1) {
    for (int x = x0; x < x1; ++x) {
        int sr = 0, sg = 0, sb = 0;
        int count = 0;
        for (int y = -radius; y <= radius; ++y) {
            int yi = qBound(0, y, h - 1);
            QRgb p = tmp[size_t(yi) * w + x];
            sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
            ++count;
        }
        for (int y = 0; y < h; ++y) {
            QRgb* px = reinterpret_cast<QRgb*>(base + qsizetype(y) * bpl) + x;
            *px = qRgba(sr / count, sg / count, sb / count, qAlpha(*px));
            int yAdd = qBound(0, y + radius + 1, h - 1);
            int ySub = qBound(0, y - radius,     h - 1);
            QRgb pa = tmp[size_t(yAdd) * w + x], ps = tmp[size_t(ySub) * w + x];
            sr += qRed(pa) - qRed(ps);
            sg += qGreen(pa) - qGreen(ps);
            sb += qBlue(pa) - qBlue(ps);
        }
    }
    });
}

// ---------------------------------------------------------------------------
// Point operations
// ---------------------------------------------------------------------------

void ImageAdjuster::saturate(QImage& img, int saturation)
{
    const int s = qBound(0, 100 + saturation, 200);   // 0..200, 100 = identity
    img.detach();   // see applyLut: no COW inside parallel loops
    const int w = img.width(), h = img.height();
    parallelRows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            int r = qRed(p), g = qGreen(p), b = qBlue(p);
            int gray = (r * 54 + g * 183 + b * 19) >> 8;
            r = clamp255(gray + (r - gray) * s / 100);
            g = clamp255(gray + (g - gray) * s / 100);
            b = clamp255(gray + (b - gray) * s / 100);
            line[x] = qRgba(r, g, b, qAlpha(p));
        }
    }
    });
}

void ImageAdjuster::edgeEnhance(QImage& img, int amount)
{
    // Laplacian edge response added back to the original.
    // Differs from unsharp mask: operates on luminance gradients, not per-channel
    // blur differences, so flat regions stay untouched while edges gain contrast.
    const int w = img.width(), h = img.height();
    if (w < 3 || h < 3) return;

    // Compute per-pixel Laplacian of luminance: 4*C - L - R - T - B
    std::vector<int> lap(size_t(w) * size_t(h), 0);
    parallelRows(h - 2, [&](int r0, int r1) {
    for (int y = r0 + 1; y < r1 + 1; ++y) {
        const QRgb* lc = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        const QRgb* lt = reinterpret_cast<const QRgb*>(img.constScanLine(y - 1));
        const QRgb* lb = reinterpret_cast<const QRgb*>(img.constScanLine(y + 1));
        auto lum = [](QRgb p) { return (qRed(p) * 54 + qGreen(p) * 183 + qBlue(p) * 19) >> 8; };
        for (int x = 1; x < w - 1; ++x)
            lap[size_t(y) * w + x] = 4 * lum(lc[x]) - lum(lc[x-1]) - lum(lc[x+1])
                                     - lum(lt[x]) - lum(lb[x]);
    }
    });

    // Blend the Laplacian response back; scale so 100% gives a strong but not
    // destructive boost (max response ~4*255=1020, we add at most ~30% of that).
    img.detach();   // see applyLut: no COW inside parallel loops
    const float strength = amount / 100.0f * 0.30f;
    parallelRows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int e = qRound(lap[size_t(y) * w + x] * strength);
            QRgb p = line[x];
            line[x] = qRgba(clamp255(qRed(p)   + e),
                            clamp255(qGreen(p) + e),
                            clamp255(qBlue(p)  + e),
                            qAlpha(p));
        }
    }
    });
}

void ImageAdjuster::unsharpMask(QImage& img, int strength, int radius)
{
    QImage blurred = img;
    boxBlur(blurred, qMax(1, radius));

    img.detach();   // see applyLut: `blurred = img` above re-shares the buffer
    const float amount = strength / 100.0f * 1.5f;
    const int w = img.width(), h = img.height();
    parallelRows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
        QRgb*       d = reinterpret_cast<QRgb*>(img.scanLine(y));
        const QRgb* b = reinterpret_cast<const QRgb*>(blurred.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            int r  = clamp255(qRound(qRed(d[x])   + (qRed(d[x])   - qRed(b[x]))   * amount));
            int g  = clamp255(qRound(qGreen(d[x]) + (qGreen(d[x]) - qGreen(b[x])) * amount));
            int bl = clamp255(qRound(qBlue(d[x])  + (qBlue(d[x])  - qBlue(b[x]))  * amount));
            d[x] = qRgba(r, g, bl, qAlpha(d[x]));
        }
    }
    });
}

void ImageAdjuster::addGrain(QImage& img, int amount)
{
    const float amp = amount * 1.6f;
    img.detach();   // see applyLut: no COW inside parallel loops
    const int w = img.width(), h = img.height();
    parallelRows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            quint32 hsh = hash2d(quint32(x), quint32(y));
            float n = (float(hsh & 0xFFFF) / 65535.0f * 2.0f - 1.0f) * amp;
            QRgb p = line[x];
            line[x] = qRgba(clamp255(qRed(p)   + int(n)),
                            clamp255(qGreen(p) + int(n)),
                            clamp255(qBlue(p)  + int(n)),
                            qAlpha(p));
        }
    }
    });
}

void ImageAdjuster::applyThreshold(QImage& img, int threshold)
{
    // threshold == 0 means disabled (checked by caller, but guard here too)
    if (threshold <= 0) return;

    img.detach();   // see applyLut: no COW inside parallel loops
    const int w = img.width(), h = img.height();
    parallelRows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            int lum = (qRed(p) * 54 + qGreen(p) * 183 + qBlue(p) * 19) >> 8;
            int v = (lum >= threshold) ? 255 : 0;
            line[x] = qRgba(v, v, v, qAlpha(p));
        }
    }
    });
}
