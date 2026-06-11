#pragma once

#include "Params.h"
#include <QImage>

/**
 * DitherRenderer
 *
 * Quantizes the image with one of several dithering strategies:
 *  - Error diffusion: Floyd–Steinberg, Jarvis–Judice–Ninke, Burkes,
 *    Atkinson, Row/Column modulation (1-D diffusion).
 *  - Ordered: Bayer (2/4/8/16), Dispersed (interleaved gradient noise),
 *    Heavy (clustered-dot screen), Circuit (XOR pattern).
 *
 * Color targets follow the tonal settings:
 *  - Image colors  → per-channel quantization to `levels` steps.
 *  - 1 tone        → binary ink-on-transparent (background shows through).
 *  - 2+ tones      → full-coverage mapping onto the tone palette.
 */
class DitherRenderer
{
public:
    // Returns an image the same size as `input`.
    static QImage render(const QImage& input, const DitherSettings& s);

private:
    static bool isOrdered(DitherAlgorithm a);
    static void renderDiffusion(const QImage& work, QImage& out, const DitherSettings& s);
    static void renderOrdered  (const QImage& work, QImage& out, const DitherSettings& s);
};
