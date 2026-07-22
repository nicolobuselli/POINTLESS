#pragma once

#include "Params.h"
#include <QImage>

/**
 * MetalEmbossRenderer
 *
 * Bump-mapped relief: luminance is treated as a height field, its Sobel
 * gradient approximates a surface normal, then a directional light (angle/
 * altitude) is applied — diffuse term tints with the metal colour,
 * specular term adds a highlight toward white. Classic "chrome/steel
 * emboss" look.
 */
class MetalEmbossRenderer
{
public:
    static QImage render(const QImage& input, const MetalEmbossSettings& params);

    // GPU pass support (filter.frag op 7): single-pass Sobel + Blinn-Phong,
    // no dilation needed. Palette falls back to CPU (OkLab matching).
    static bool gpuRenderable(const MetalEmbossSettings& s);
};
