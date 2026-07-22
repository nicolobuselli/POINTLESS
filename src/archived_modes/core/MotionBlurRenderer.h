#pragma once

#include "Params.h"
#include <QImage>

/**
 * MotionBlurRenderer
 *
 * Directional streak blur: each output pixel averages samples taken along
 * one axis (angle/distance), bilinear-sampled and accumulated in linear
 * light so the average reflectance matches the source (same convention as
 * every other renderer's tonal quantities). CPU-only, whole-image filter —
 * no GPU pass (v1, mirrors Halftone's raster-only launch).
 */
class MotionBlurRenderer
{
public:
    static QImage render(const QImage& input, const MotionBlurSettings& params);
};
