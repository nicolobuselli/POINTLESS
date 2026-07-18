#pragma once

#include "Params.h"
#include <QImage>
#include <QPainter>

// ============================================================
//  HalftoneRenderer — canonical AM (amplitude-modulated) print
//  screen: the source is separated into four CMYK channels, each
//  screened on its own square lattice rotated to its own angle
//  (classic rosette). Dot area tracks ink coverage; past 50% the
//  dot inverts into a shrinking white hole in a full cell. The
//  four channel images are multiplied together over white paper.
//
//  v1 is raster-only (no renderVector — SVG export embeds the
//  raster) and has no tonal palette or localization: the inks are
//  the four process colours, derived from the source pixels.
// ============================================================

class HalftoneRenderer
{
public:
    HalftoneRenderer() = default;

    void render(const QImage& input, QPainter& output, const HalftoneSettings& params);

    // Upper bound on drawn dots (4 screens × cell count), for the export
    // "heavy render" estimate.
    static int estimateDotCount(const QImage& input, const HalftoneSettings& params);

    // GPU path (preview): halftone.frag handles every shape; only the UBO's
    // 8-screen cap and a removed Fill fall back to the CPU renderer.
    static bool gpuRenderable(const HalftoneSettings& params)
    {
        return params.tonal.enabled
            && (params.tonal.mode == ToneMode::ImageColors
                || params.tonal.tones.size() <= 8);
    }
};
