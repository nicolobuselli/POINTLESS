#pragma once

#include "Params.h"
#include <QImage>

/**
 * ThermalRenderer
 *
 * False-color remap driven by luminance: the classic "Iron" thermal-camera
 * ramp (black → purple → red → orange → yellow → white) by default, or the
 * shared Fill tones when the user picks FixedTones/Palette (same nearest-
 * tone rule as Mosaic/Dither's simple, non-diffused path). An optional box
 * blur softens the source first for the camera's characteristic soft blobs.
 */
class ThermalRenderer
{
public:
    static QImage render(const QImage& input, const ThermalSettings& params);

    // GPU pass support (filter.frag ops 0/1/2): the whole mode is per-pixel
    // (+ a separable pre-blur) — Palette falls back to CPU (OkLab matching).
    static bool gpuRenderable(const ThermalSettings& s);
};
