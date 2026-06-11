#pragma once

#include "Params.h"
#include <QImage>

/**
 * ImageAdjuster
 *
 * Applies the global left-panel adjustments to the source image
 * before it is handed to the active renderer.
 *
 * Pipeline order:
 *   resize → brightness → contrast → gamma → levels → saturation →
 *   blur → edge enhancement → sharpen → grain → posterize → threshold
 */
class ImageAdjuster
{
public:
    static QImage apply(const QImage& src, const Adjustments& a);

private:
    static void boxBlur(QImage& img, int radius);
    static void blend(QImage& dst, const QImage& other, float t);
    static void brightnessContrast(QImage& img, int brightness, int contrast);
    static void applyGamma(QImage& img, float gamma);
    static void applyLevels(QImage& img, int blackPoint, float midPoint, int whitePoint);
    static void saturate(QImage& img, int saturation);
    static void edgeEnhance(QImage& img, int amount);
    static void unsharpMask(QImage& img, int strength, int radius);
    static void addGrain(QImage& img, int amount);
    static void applyPosterize(QImage& img, int levels);
    static void applyThreshold(QImage& img, int threshold);
};
