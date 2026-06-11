#include "ImageAdjuster.h"

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

} // namespace

// ---------------------------------------------------------------------------
// Public entry point
// Pipeline: resize → brightness → contrast → gamma → levels → saturation →
//           blur → edge enhancement → sharpen → grain → posterize → threshold
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

    // 2. Brightness / Contrast
    if (a.brightness != 0 || a.contrast != 0)
        brightnessContrast(img, a.brightness, a.contrast);

    // 3. Gamma
    if (a.gamma != 100)
        applyGamma(img, a.gamma / 100.0f);

    // 4. Levels
    if (a.levelsBlack != 0 || a.levelsMid != 100 || a.levelsWhite != 255)
        applyLevels(img, a.levelsBlack, a.levelsMid / 100.0f, a.levelsWhite);

    // 5. Saturation
    if (a.saturation != 0)
        saturate(img, a.saturation);

    // 6. Blur
    if (a.blur > 0) {
        int radius = qMax(1, qRound(a.blur * 0.35f));
        int passes = 2 + a.blur / 35;
        for (int i = 0; i < passes; ++i)
            boxBlur(img, radius);
    }

    // 7. Edge Enhancement
    if (a.edgeEnhancement > 0)
        edgeEnhance(img, a.edgeEnhancement);

    // 8. Sharpen (unsharp mask)
    if (a.sharpenStrength > 0)
        unsharpMask(img, a.sharpenStrength, a.sharpenRadius);

    // 9. Grain
    if (a.grain > 0)
        addGrain(img, a.grain);

    // 10. Posterize
    if (a.posterize < 256)
        applyPosterize(img, a.posterize);

    // 11. Threshold
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

    std::vector<quint32> tmp(size_t(w) * size_t(h));

    // Horizontal pass: img → tmp
    for (int y = 0; y < h; ++y) {
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

    // Vertical pass: tmp → img
    for (int x = 0; x < w; ++x) {
        int sr = 0, sg = 0, sb = 0;
        int count = 0;
        for (int y = -radius; y <= radius; ++y) {
            int yi = qBound(0, y, h - 1);
            QRgb p = tmp[size_t(yi) * w + x];
            sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
            ++count;
        }
        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            line[x] = qRgba(sr / count, sg / count, sb / count, qAlpha(line[x]));
            int yAdd = qBound(0, y + radius + 1, h - 1);
            int ySub = qBound(0, y - radius,     h - 1);
            QRgb pa = tmp[size_t(yAdd) * w + x], ps = tmp[size_t(ySub) * w + x];
            sr += qRed(pa) - qRed(ps);
            sg += qGreen(pa) - qGreen(ps);
            sb += qBlue(pa) - qBlue(ps);
        }
    }
}

void ImageAdjuster::blend(QImage& dst, const QImage& other, float t)
{
    const int w = dst.width(), h = dst.height();
    const int ti = qBound(0, qRound(t * 256.0f), 256);
    for (int y = 0; y < h; ++y) {
        QRgb*       d = reinterpret_cast<QRgb*>(dst.scanLine(y));
        const QRgb* o = reinterpret_cast<const QRgb*>(other.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = qRed(d[x])   + ((qRed(o[x])   - qRed(d[x]))   * ti >> 8);
            int g = qGreen(d[x]) + ((qGreen(o[x]) - qGreen(d[x])) * ti >> 8);
            int b = qBlue(d[x])  + ((qBlue(o[x])  - qBlue(d[x]))  * ti >> 8);
            d[x] = qRgba(r, g, b, qAlpha(d[x]));
        }
    }
}

// ---------------------------------------------------------------------------
// Point operations
// ---------------------------------------------------------------------------

void ImageAdjuster::brightnessContrast(QImage& img, int brightness, int contrast)
{
    const float c = contrast * 2.55f;
    const float factor = (259.0f * (c + 255.0f)) / (255.0f * (259.0f - c));
    const float offset = brightness * 1.275f;

    int lut[256];
    for (int i = 0; i < 256; ++i)
        lut[i] = clamp255(qRound(factor * (i - 128) + 128 + offset));

    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            line[x] = qRgba(lut[qRed(p)], lut[qGreen(p)], lut[qBlue(p)], qAlpha(p));
        }
    }
}

void ImageAdjuster::applyGamma(QImage& img, float gamma)
{
    if (gamma <= 0.0f) gamma = 0.01f;
    uint8_t lut[256];
    for (int i = 0; i < 256; ++i)
        lut[i] = clamp255(qRound(qPow(i / 255.0, gamma) * 255.0));

    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            line[x] = qRgba(lut[qRed(p)], lut[qGreen(p)], lut[qBlue(p)], qAlpha(p));
        }
    }
}

void ImageAdjuster::applyLevels(QImage& img, int blackPoint, float midPoint, int whitePoint)
{
    const float range = float(whitePoint - blackPoint);
    if (midPoint <= 0.0f) midPoint = 0.01f;

    uint8_t lut[256];
    for (int i = 0; i < 256; ++i) {
        float v = (range > 0.0f) ? (i - blackPoint) / range : 0.0f;
        v = qBound(0.0f, v, 1.0f);
        // midPoint > 1 brightens, < 1 darkens — same convention as Photoshop Levels
        if (midPoint != 1.0f)
            v = qPow(v, 1.0f / midPoint);
        lut[i] = clamp255(qRound(v * 255.0f));
    }

    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            line[x] = qRgba(lut[qRed(p)], lut[qGreen(p)], lut[qBlue(p)], qAlpha(p));
        }
    }
}

void ImageAdjuster::saturate(QImage& img, int saturation)
{
    const int s = qBound(0, 100 + saturation, 200);   // 0..200, 100 = identity
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
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
    for (int y = 1; y < h - 1; ++y) {
        const QRgb* lc = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        const QRgb* lt = reinterpret_cast<const QRgb*>(img.constScanLine(y - 1));
        const QRgb* lb = reinterpret_cast<const QRgb*>(img.constScanLine(y + 1));
        auto lum = [](QRgb p) { return (qRed(p) * 54 + qGreen(p) * 183 + qBlue(p) * 19) >> 8; };
        for (int x = 1; x < w - 1; ++x)
            lap[size_t(y) * w + x] = 4 * lum(lc[x]) - lum(lc[x-1]) - lum(lc[x+1])
                                     - lum(lt[x]) - lum(lb[x]);
    }

    // Blend the Laplacian response back; scale so 100% gives a strong but not
    // destructive boost (max response ~4*255=1020, we add at most ~30% of that).
    const float strength = amount / 100.0f * 0.30f;
    for (int y = 0; y < h; ++y) {
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
}

void ImageAdjuster::unsharpMask(QImage& img, int strength, int radius)
{
    QImage blurred = img;
    boxBlur(blurred, qMax(1, radius));

    const float amount = strength / 100.0f * 1.5f;
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb*       d = reinterpret_cast<QRgb*>(img.scanLine(y));
        const QRgb* b = reinterpret_cast<const QRgb*>(blurred.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            int r  = clamp255(qRound(qRed(d[x])   + (qRed(d[x])   - qRed(b[x]))   * amount));
            int g  = clamp255(qRound(qGreen(d[x]) + (qGreen(d[x]) - qGreen(b[x])) * amount));
            int bl = clamp255(qRound(qBlue(d[x])  + (qBlue(d[x])  - qBlue(b[x]))  * amount));
            d[x] = qRgba(r, g, bl, qAlpha(d[x]));
        }
    }
}

void ImageAdjuster::addGrain(QImage& img, int amount)
{
    const float amp = amount * 1.6f;
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
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
}

void ImageAdjuster::applyPosterize(QImage& img, int levels)
{
    if (levels >= 256) return;
    levels = qMax(2, levels);

    uint8_t lut[256];
    for (int i = 0; i < 256; ++i) {
        int step = qRound(float(i) / 255.0f * (levels - 1));
        lut[i] = clamp255(qRound(float(step) / float(levels - 1) * 255.0f));
    }

    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            line[x] = qRgba(lut[qRed(p)], lut[qGreen(p)], lut[qBlue(p)], qAlpha(p));
        }
    }
}

void ImageAdjuster::applyThreshold(QImage& img, int threshold)
{
    // threshold == 0 means disabled (checked by caller, but guard here too)
    if (threshold <= 0) return;

    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            int lum = (qRed(p) * 54 + qGreen(p) * 183 + qBlue(p) * 19) >> 8;
            int v = (lum >= threshold) ? 255 : 0;
            line[x] = qRgba(v, v, v, qAlpha(p));
        }
    }
}
