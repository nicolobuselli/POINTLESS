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
// ---------------------------------------------------------------------------

QImage ImageAdjuster::apply(const QImage& src, const Adjustments& a)
{
    if (src.isNull()) return src;

    QImage img = src.convertToFormat(QImage::Format_ARGB32);

    // Size
    if (a.sizePct != 100) {
        int w = qMax(8, img.width()  * a.sizePct / 100);
        int h = qMax(8, img.height() * a.sizePct / 100);
        img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    // Denoise: blend toward a small box blur
    if (a.denoise > 0) {
        QImage blurred = img;
        int denoiseRadius = qMax(1, qRound(1.0f + a.denoise * 0.06f));
        boxBlur(blurred, denoiseRadius);
        float denoiseBlend = qBound(0.0f, a.denoise / 70.0f, 1.0f);
        blend(img, blurred, denoiseBlend);
    }

    // Blur
    if (a.blur > 0) {
        int radius = qMax(1, qRound(a.blur * 0.35f));
        int passes = 2 + a.blur / 35;
        for (int i = 0; i < passes; ++i)
            boxBlur(img, radius);
    }

    // Sharpen (unsharp mask)
    if (a.sharpenStrength > 0)
        unsharpMask(img, a.sharpenStrength, a.sharpenRadius);

    // Brightness / contrast
    if (a.brightness != 0 || a.contrast != 0)
        brightnessContrast(img, a.brightness, a.contrast);

    // Saturation
    if (a.saturation != 0)
        saturate(img, a.saturation);

    // Noise
    if (a.noise > 0)
        addNoise(img, a.noise);

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

void ImageAdjuster::addNoise(QImage& img, int amount)
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
            int r = clamp255(qRound(qRed(d[x])   + (qRed(d[x])   - qRed(b[x]))   * amount));
            int g = clamp255(qRound(qGreen(d[x]) + (qGreen(d[x]) - qGreen(b[x])) * amount));
            int bl = clamp255(qRound(qBlue(d[x]) + (qBlue(d[x])  - qBlue(b[x]))  * amount));
            d[x] = qRgba(r, g, bl, qAlpha(d[x]));
        }
    }
}
