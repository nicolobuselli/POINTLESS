#include "BlendCompositor.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {

// ── Separable blend functions (per channel, values in 0..1) ──

inline float blendColorBurn(float b, float s)
{
    if (b >= 1.0f) return 1.0f;
    if (s <= 0.0f) return 0.0f;
    return 1.0f - std::min(1.0f, (1.0f - b) / s);
}

inline float blendColorDodge(float b, float s)
{
    if (b <= 0.0f) return 0.0f;
    if (s >= 1.0f) return 1.0f;
    return std::min(1.0f, b / (1.0f - s));
}

inline float blendVividLight(float b, float s)
{
    return (s <= 0.5f) ? blendColorBurn(b, 2.0f * s)
                       : blendColorDodge(b, 2.0f * s - 1.0f);
}

// ── Non-separable helpers (PDF spec: Lum / Sat on the color triple) ──

struct Rgb { float r, g, b; };

inline float lum(const Rgb& c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }

inline Rgb clipColor(Rgb c)
{
    const float l = lum(c);
    const float n = std::min({ c.r, c.g, c.b });
    const float x = std::max({ c.r, c.g, c.b });
    if (n < 0.0f) {
        c.r = l + (c.r - l) * l / (l - n);
        c.g = l + (c.g - l) * l / (l - n);
        c.b = l + (c.b - l) * l / (l - n);
    }
    if (x > 1.0f) {
        c.r = l + (c.r - l) * (1.0f - l) / (x - l);
        c.g = l + (c.g - l) * (1.0f - l) / (x - l);
        c.b = l + (c.b - l) * (1.0f - l) / (x - l);
    }
    return c;
}

inline Rgb setLum(Rgb c, float l)
{
    const float d = l - lum(c);
    c.r += d; c.g += d; c.b += d;
    return clipColor(c);
}

inline float sat(const Rgb& c)
{
    return std::max({ c.r, c.g, c.b }) - std::min({ c.r, c.g, c.b });
}

inline Rgb setSat(Rgb c, float s)
{
    float* mn = &c.r; float* md = &c.g; float* mx = &c.b;
    if (*mn > *md) std::swap(mn, md);
    if (*md > *mx) std::swap(md, mx);
    if (*mn > *md) std::swap(mn, md);
    if (*mx > *mn) {
        *md = (*md - *mn) * s / (*mx - *mn);
        *mx = s;
    } else {
        *md = *mx = 0.0f;
    }
    *mn = 0.0f;
    return c;
}

// Stable per-pixel pseudo-random in [0,1) for Dissolve.
inline float hashRand(int x, int y)
{
    quint32 h = quint32(x) * 73856093u ^ quint32(y) * 19349663u;
    h = (h ^ (h >> 13)) * 0x5BD1E995u;
    return float(h & 0xFFFFu) / 65536.0f;
}

inline Rgb blendNonSeparable(const Rgb& cb, const Rgb& cs, BlendMode mode)
{
    switch (mode) {
        case BlendMode::Hue:        return setLum(setSat(cs, sat(cb)), lum(cb));
        case BlendMode::Saturation: return setLum(setSat(cb, sat(cs)), lum(cb));
        case BlendMode::Color:      return setLum(cs, lum(cb));
        case BlendMode::Luminosity: return setLum(cb, lum(cs));
        case BlendMode::DarkerColor:  return (lum(cs) < lum(cb)) ? cs : cb;
        case BlendMode::LighterColor: return (lum(cs) > lum(cb)) ? cs : cb;
        default:                    return cs;
    }
}

inline bool isNonSeparable(BlendMode m)
{
    return m == BlendMode::Hue || m == BlendMode::Saturation
        || m == BlendMode::Color || m == BlendMode::Luminosity
        || m == BlendMode::DarkerColor || m == BlendMode::LighterColor;
}

inline float blendSeparable(float b, float s, BlendMode mode)
{
    switch (mode) {
        case BlendMode::LinearBurn:  return std::max(0.0f, b + s - 1.0f);
        case BlendMode::VividLight:  return blendVividLight(b, s);
        case BlendMode::LinearLight: return std::clamp(b + 2.0f * s - 1.0f, 0.0f, 1.0f);
        case BlendMode::PinLight:    return (s <= 0.5f) ? std::min(b, 2.0f * s)
                                                        : std::max(b, 2.0f * s - 1.0f);
        case BlendMode::HardMix:     return (blendVividLight(b, s) < 0.5f) ? 0.0f : 1.0f;
        case BlendMode::Subtract:    return std::max(0.0f, b - s);
        case BlendMode::Divide:      return (s <= 0.0f) ? 1.0f : std::min(1.0f, b / s);
        default:                     return s;
    }
}

// Per-pixel compositor for modes QPainter does not provide.
void compositeManual(QImage& base, const QImage& layer, BlendMode mode)
{
    QImage b = base.convertToFormat(QImage::Format_ARGB32);
    QImage s = layer.convertToFormat(QImage::Format_ARGB32);

    const int w = b.width();
    const int h = b.height();
    const bool nonSep = isNonSeparable(mode);

    for (int y = 0; y < h; ++y) {
        QRgb*       bl = reinterpret_cast<QRgb*>(b.scanLine(y));
        const QRgb* sl = reinterpret_cast<const QRgb*>(s.constScanLine(y));

        for (int x = 0; x < w; ++x) {
            const QRgb sp = sl[x];
            const float sa = qAlpha(sp) / 255.0f;
            if (sa <= 0.0f) continue;

            const QRgb bp = bl[x];
            const float ba = qAlpha(bp) / 255.0f;

            const Rgb cs { qRed(sp) / 255.0f, qGreen(sp) / 255.0f, qBlue(sp) / 255.0f };
            const Rgb cb { qRed(bp) / 255.0f, qGreen(bp) / 255.0f, qBlue(bp) / 255.0f };

            if (mode == BlendMode::Dissolve) {
                // Alpha-based dithering: source wins with probability sa.
                if (hashRand(x, y) < sa)
                    bl[x] = qRgba(qRed(sp), qGreen(sp), qBlue(sp), 255);
                continue;
            }

            Rgb mixed;
            if (nonSep) {
                mixed = blendNonSeparable(cb, cs, mode);
            } else {
                mixed.r = blendSeparable(cb.r, cs.r, mode);
                mixed.g = blendSeparable(cb.g, cs.g, mode);
                mixed.b = blendSeparable(cb.b, cs.b, mode);
            }

            // W3C compositing: Co = αs(1−αb)Cs + αs·αb·B(Cb,Cs) + (1−αs)αb·Cb
            const float ao = sa + ba * (1.0f - sa);
            if (ao <= 0.0f) { bl[x] = qRgba(0, 0, 0, 0); continue; }

            auto channel = [&](float csv, float cbv, float mv) {
                const float co = sa * (1.0f - ba) * csv + sa * ba * mv + (1.0f - sa) * ba * cbv;
                return qBound(0, qRound(co / ao * 255.0f), 255);
            };

            bl[x] = qRgba(channel(cs.r, cb.r, mixed.r),
                          channel(cs.g, cb.g, mixed.g),
                          channel(cs.b, cb.b, mixed.b),
                          qBound(0, qRound(ao * 255.0f), 255));
        }
    }

    base = b.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace

namespace BlendCompositor {

void compositeOver(QImage& base, const QImage& layer, BlendMode mode)
{
    if (base.isNull() || layer.isNull()) return;
    if (base.format() != QImage::Format_ARGB32_Premultiplied)
        base = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QPainter::CompositionMode cm = QPainter::CompositionMode_SourceOver;
    bool native = true;
    switch (mode) {
        case BlendMode::Normal:      cm = QPainter::CompositionMode_SourceOver;  break;
        case BlendMode::Darken:      cm = QPainter::CompositionMode_Darken;      break;
        case BlendMode::Multiply:    cm = QPainter::CompositionMode_Multiply;    break;
        case BlendMode::ColorBurn:   cm = QPainter::CompositionMode_ColorBurn;   break;
        case BlendMode::Lighten:     cm = QPainter::CompositionMode_Lighten;     break;
        case BlendMode::Screen:      cm = QPainter::CompositionMode_Screen;      break;
        case BlendMode::ColorDodge:  cm = QPainter::CompositionMode_ColorDodge;  break;
        case BlendMode::LinearDodge: cm = QPainter::CompositionMode_Plus;        break;
        case BlendMode::Overlay:     cm = QPainter::CompositionMode_Overlay;     break;
        case BlendMode::SoftLight:   cm = QPainter::CompositionMode_SoftLight;   break;
        case BlendMode::HardLight:   cm = QPainter::CompositionMode_HardLight;   break;
        case BlendMode::Difference:  cm = QPainter::CompositionMode_Difference;  break;
        case BlendMode::Exclusion:   cm = QPainter::CompositionMode_Exclusion;   break;
        default:                     native = false;                             break;
    }

    if (native) {
        QPainter p(&base);
        p.setCompositionMode(cm);
        p.drawImage(0, 0, layer);
        return;
    }

    compositeManual(base, layer, mode);
}

} // namespace BlendCompositor
