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
    // The point ops (brightness/contrast, gamma, levels, invert, posterize)
    // are 256-entry tables composed into a single pass inside apply() — see
    // LutChain there. Only the ops that can't be a per-channel table stay
    // here as real image passes.
    static void boxBlur(QImage& img, int radius);
    static void saturate(QImage& img, int saturation);
    static void edgeEnhance(QImage& img, int amount);
    static void unsharpMask(QImage& img, int strength, int radius);
    static void addGrain(QImage& img, int amount);
    static void applyThreshold(QImage& img, int threshold);
};
