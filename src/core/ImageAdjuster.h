#pragma once

#include "Params.h"
#include <QImage>

/**
 * ImageAdjuster
 *
 * Applies the global left-panel adjustments to the source image
 * before it is handed to the active renderer.
 * Order: size → denoise → blur → sharpen → brightness/contrast →
 *        saturation → noise.
 */
class ImageAdjuster
{
public:
    static QImage apply(const QImage& src, const Adjustments& a);

private:
    static void boxBlur(QImage& img, int radius);
    static void blend(QImage& dst, const QImage& other, float t);   // dst = lerp(dst, other, t)
    static void brightnessContrast(QImage& img, int brightness, int contrast);
    static void saturate(QImage& img, int saturation);
    static void addNoise(QImage& img, int amount);
    static void unsharpMask(QImage& img, int strength, int radius);
};
