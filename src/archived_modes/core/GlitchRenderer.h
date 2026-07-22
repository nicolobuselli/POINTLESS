#pragma once

#include "Params.h"
#include <QImage>

/**
 * GlitchRenderer
 *
 * Four glitch-art techniques picked via GlitchSettings::algorithm (like
 * Dither's algorithm dropdown):
 *  Glitch     — horizontal bands wrap-shifted by a seeded random amount,
 *               then RGB channels split horizontally (chromatic aberration).
 *               (This used to be called "Datamosh" — renamed once the real
 *               datamoshing technique below was added.)
 *  Pixel Sort — contiguous runs of pixels brighter than a threshold are
 *               sorted by luminance along rows or columns (classic streak).
 *  CRT/Scanline — darkened scanlines + RGB aperture-grille subpixel mask.
 *  Datamosh   — macroblock corruption: each block has a chance to be
 *               replaced by a motion-compensated but wrong source block,
 *               the classic "frozen/smeared block" corrupted-P-frame look.
 * Colour follows the shared Fill system same as Motion Blur/Thermal's
 * simple (non-diffused) path.
 */
class GlitchRenderer
{
public:
    static QImage render(const QImage& input, const GlitchSettings& params);

    // GPU pass support (filter.frag ops 8/9): Glitch and CRT are per-pixel
    // (band hash + fixed-offset samples). Pixel Sort stays CPU forever — its
    // runs are data-dependent sorts, not a parallel per-pixel op. Datamosh
    // (macroblock corruption) also stays CPU for now (2D block hashing not
    // yet ported to the shader). Palette falls back to CPU too (OkLab
    // matching).
    static bool gpuRenderable(const GlitchSettings& s);
};
