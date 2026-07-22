#pragma once

#include "Params.h"
#include <QImage>

/**
 * BlueprintRenderer
 *
 * Technical-drawing look: Sobel edge magnitude over perceptual luma,
 * thresholded and optionally dilated (line width), drawn over a flat
 * "paper" colour. Line colour follows the shared Fill system: ImageColors
 * (default) = the source's own colour at each edge; FixedTones = tone
 * picked by edge strength (pickToneIndex, reusing magnitude as a pseudo-
 * luma); Palette = nearest palette match to the source's local colour.
 */
class BlueprintRenderer
{
public:
    static QImage render(const QImage& input, const BlueprintSettings& params);

    // GPU pass support (filter.frag ops 3/4/5/6): Sobel + separable box-max
    // dilation + threshold fill, all embarrassingly parallel. Palette falls
    // back to CPU (OkLab matching).
    static bool gpuRenderable(const BlueprintSettings& s);
};
