#pragma once

#include "Params.h"    // ToneEntry (for palette helpers)
#include <QColor>      // QRgb, qRed/qGreen/qBlue
#include <QtGlobal>
#include <array>
#include <cmath>
#include <vector>

// ============================================================
//  Color math — sRGB <-> linear-light conversions
//
//  All tonal *quantities* (ink coverage, dot area, dither error
//  diffusion, band density) must be computed in linear light so the
//  average reflectance of the output matches the source. The 8-bit
//  sRGB encoding is gamma-companded: processing the raw channel
//  values directly makes the midtones too dark.
//
//  Tone / shape / glyph *selection*, instead, keeps using
//  perceptualLuma() (the gamma-encoded weighted luma) so the
//  user-facing 0..255 tone "level" anchors keep their meaning.
// ============================================================

namespace ColorMath {

// sRGB 8-bit (0..255) → linear light (0..1). Standard sRGB EOTF, built once.
inline const std::array<float, 256>& srgbToLinearLUT()
{
    static const std::array<float, 256> lut = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const float c = float(i) / 255.0f;
            t[size_t(i)] = (c <= 0.04045f) ? c / 12.92f
                                           : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return lut;
}

// sRGB channel byte → linear (0..1).
inline float srgbToLinear(int v8)
{
    return srgbToLinearLUT()[size_t(v8 & 0xFF)];
}

// linear (0..1) → sRGB channel byte (0..255).
inline int linearToSrgb8(float lin)
{
    lin = lin < 0.0f ? 0.0f : (lin > 1.0f ? 1.0f : lin);
    const float s = (lin <= 0.0031308f) ? lin * 12.92f
                                        : 1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f;
    const int v = int(s * 255.0f + 0.5f);
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

// Linear-light relative luminance from an sRGB pixel (0..1).
inline float linearLuminance(QRgb p)
{
    const auto& lut = srgbToLinearLUT();
    return 0.2126f * lut[size_t(qRed(p))]
         + 0.7152f * lut[size_t(qGreen(p))]
         + 0.0722f * lut[size_t(qBlue(p))];
}

// Perceptual (gamma-encoded) weighted luma, 0..1 — for tone/shape/glyph
// selection so the 0..255 "level" sliders keep their meaning.
inline float perceptualLuma(QRgb p)
{
    return (0.2126f * float(qRed(p))
          + 0.7152f * float(qGreen(p))
          + 0.0722f * float(qBlue(p))) / 255.0f;
}

// Perceptual weighted luma (0..1) of already-linear channels (0..1).
// Used when the working buffer is in linear light but the *selection*
// of a tone must still happen in the perceptual domain.
inline float perceptualLumaFromLinear(float rLin, float gLin, float bLin)
{
    return (0.2126f * float(linearToSrgb8(rLin))
          + 0.7152f * float(linearToSrgb8(gLin))
          + 0.0722f * float(linearToSrgb8(bLin))) / 255.0f;
}

// ============================================================
//  OkLab — perceptually-uniform colour space, for nearest-colour
//  palette matching. Input is linear-light RGB (0..1).
// ============================================================

struct OkLab { float L, a, b; };

inline OkLab linearToOklab(float r, float g, float b)
{
    const float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    const float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    const float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;
    const float l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);
    return {
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_
    };
}

// ============================================================
//  Palette — precomputed view of a tone list for nearest-colour
//  dithering: each entry carries its linear RGB (the target used
//  for error diffusion), its OkLab coordinates (for the distance
//  search) and the sRGB output pixel (with per-tone opacity).
// ============================================================

struct PaletteEntry {
    float rLin, gLin, bLin;
    OkLab lab;
    QRgb  out;
};

inline std::vector<PaletteEntry> buildPalette(const std::vector<ToneEntry>& tones)
{
    std::vector<PaletteEntry> pal;
    pal.reserve(tones.size());
    for (const ToneEntry& t : tones) {
        const QColor& c = t.color;
        const float r = srgbToLinear(c.red());
        const float g = srgbToLinear(c.green());
        const float b = srgbToLinear(c.blue());
        const int   a = qRound(qBound(0.0f, t.opacity, 1.0f) * 255.0f);
        pal.push_back({ r, g, b, linearToOklab(r, g, b),
                        qRgba(c.red(), c.green(), c.blue(), a) });
    }
    return pal;
}

// Index of the palette entry closest to `c` (squared OkLab distance).
inline int nearestPaletteIndex(const std::vector<PaletteEntry>& pal, const OkLab& c)
{
    int   best = 0;
    float bestD = 1e30f;
    for (int i = 0; i < int(pal.size()); ++i) {
        const float dL = pal[i].lab.L - c.L;
        const float da = pal[i].lab.a - c.a;
        const float db = pal[i].lab.b - c.b;
        const float d  = dL * dL + da * da + db * db;
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

} // namespace ColorMath
