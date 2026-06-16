#pragma once

#include "Params.h"
#include <QImage>

/**
 * DitherRenderer
 *
 * Quantizes an image using one of three strategy families:
 *
 *  Error Diffusion — Floyd–Steinberg, False Floyd–Steinberg, Atkinson,
 *    Burkes, Sierra, Sierra Lite, Jarvis–Judice–Ninke, Stucki.
 *    Each pixel is quantized in scan order; the residual error is
 *    propagated to unprocessed neighbours via a fixed kernel.
 *
 *  Ordered Dithering — Bayer (2/4/8/16), Clustered Dot (4/8),
 *    Blue Noise (64-sample V&C mask), Void and Cluster (32-sample V&C).
 *    Per-pixel threshold is looked up from a precomputed mask; the
 *    operation is fully parallel and scales to any resolution.
 *
 *  Hybrid — Dot Diffusion.
 *    Pixels are processed in the order defined by a class matrix
 *    (Knuth, 1987) while still propagating quantization error to
 *    unprocessed neighbours, combining the structure of ordered
 *    dithering with the tonal accuracy of error diffusion.
 *
 * Color output follows the tonal settings:
 *   ImageColors → per-channel quantization to 2 steps.
 *   1 tone      → binary ink-on-transparent (background visible).
 *   2+ tones    → luminosity mapped onto the tone palette via each
 *                 tone's level anchor (see pickToneIndex).
 */
class DitherRenderer
{
public:
    static QImage render(const QImage& input, const DitherSettings& s);

private:
    static bool isOrdered  (DitherAlgorithm a);
    static bool isHybrid   (DitherAlgorithm a);
    static bool isThreshold(DitherAlgorithm a);

    static void renderDiffusion   (const QImage& work, QImage& out, const DitherSettings& s);
    static void renderOrdered     (const QImage& work, QImage& out, const DitherSettings& s);
    static void renderDotDiffusion(const QImage& work, QImage& out, const DitherSettings& s);
    static void renderThreshold   (const QImage& work, QImage& out, const DitherSettings& s);
};
