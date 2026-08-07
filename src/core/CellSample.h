#pragma once

#include "ColorMath.h"
#include <QColor>
#include <QImage>

// ============================================================
//  CellSample
//
//  Every tone mode (Dot Grid, ASCII, Mosaic, Halftone) works the
//  same way: pick a sampling cell around a grid point, average it,
//  and turn that average into a symbol size / glyph / tile colour.
//  The averaging half of that lived in four near-identical copies;
//  this is the one copy.
//
//  Averaging happens in LINEAR light — see CLAUDE.md §10: tonal
//  *quantities* must be linear or the mean reflectance of the
//  output stops matching the source.
// ============================================================

namespace CellSample {

// Clamped sampling cell of side `cellPx` centred on (sx, sy).
inline void around(float sx, float sy, int cellPx, int imgW, int imgH,
                   int& cx, int& cy, int& cw, int& ch)
{
    cellPx = qMax(1, cellPx);
    cx = qBound(0, qRound(sx) - cellPx / 2, qMax(0, imgW - 1));
    cy = qBound(0, qRound(sy) - cellPx / 2, qMax(0, imgH - 1));
    cw = qBound(1, cellPx, imgW - cx);
    ch = qBound(1, cellPx, imgH - cy);
}

struct Mean {
    // Channel means in linear light. Default = white, which is what the
    // luminance callers used to return for an empty cell.
    float rLin = 1.0f, gLin = 1.0f, bLin = 1.0f;

    // Mean of the per-pixel linear luminance. Algebraically identical to
    // weighting the channel means above, but accumulated separately so this
    // stays bit-for-bit what the old per-mode helpers produced.
    float lumLin = 1.0f;

    // No pixels in the cell. The three call sites disagree on what that
    // should mean (white / black / no coverage), so each keeps its own
    // fallback rather than this header picking one.
    bool empty = true;

    // Perceptual tone of the cell, 0..1 — for *selection* (which tone, which
    // glyph), never for quantities.
    //
    // WARNING: the two derivations below are NOT equal, and the codebase uses
    // both. sRGB-encoding the luminance is not the same as weighting the
    // sRGB-encoded channels. Collapsing them into one would shift tone and
    // glyph selection in whichever modes got switched, so both stay.
    float tonePerc() const              // Dot Grid, ASCII
    { return ColorMath::linearToSrgb8(lumLin) / 255.0f; }

    float tonePercPerChannel() const    // Mosaic, Halftone
    { return ColorMath::perceptualLumaFromLinear(rLin, gLin, bLin); }

    // The cell's average colour, re-encoded to sRGB. Averaging the raw bytes
    // instead would skew toward the darker side.
    QColor srgb() const
    {
        return QColor(ColorMath::linearToSrgb8(rLin),
                      ColorMath::linearToSrgb8(gLin),
                      ColorMath::linearToSrgb8(bLin));
    }
};

// One pass over the cell for both the colour and the luminance — the callers
// that need both used to walk it twice.
inline Mean mean(const QImage& rgb, int cx, int cy, int cw, int ch)
{
    double r = 0.0, g = 0.0, b = 0.0, lum = 0.0;
    int count = 0;
    for (int y = cy; y < cy + ch; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = cx; x < cx + cw; ++x) {
            const QRgb px = line[x];
            r   += ColorMath::srgbToLinear(qRed(px));
            g   += ColorMath::srgbToLinear(qGreen(px));
            b   += ColorMath::srgbToLinear(qBlue(px));
            lum += ColorMath::linearLuminance(px);
            ++count;
        }
    }
    Mean m;
    if (count == 0) return m;
    m.rLin   = float(r   / count);
    m.gLin   = float(g   / count);
    m.bLin   = float(b   / count);
    m.lumLin = float(lum / count);
    m.empty  = false;
    return m;
}

} // namespace CellSample
