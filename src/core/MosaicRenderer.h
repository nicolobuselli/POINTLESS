#pragma once

#include "Params.h"
#include <QImage>
#include <QPainter>
#include <QRect>
#include <array>

struct MosaicGpuTextAtlas {
    QImage image;
    std::array<QRect, 8> rects{};
};

/**
 * MosaicRenderer
 *
 * Rectangular tile grid: each tile averages the source under it, takes the
 * tone nearest its perceptual luminosity (shared tonal system) and is filled
 * with that tone's colour. Each tone can carry a text label, drawn centred
 * and scaled to fit the tile minus the configured padding. Painter-based so
 * SVG export keeps fills and text as vectors.
 */
class MosaicRenderer
{
public:
    static void render(const QImage& input, QPainter& output, const MosaicSettings& params);

    // GPU pass support (mosaic.vert/.frag): instanced tile fill + cached text
    // atlas. Palette fill uses OkLab on both paths, up to the UBO's 8-tone
    // capacity. A removed Fill still paints nothing.
    static bool gpuRenderable(const MosaicSettings& s)
    {
        if (!s.tonal.enabled) return false;
        if (s.tonal.mode != ToneMode::ImageColors && s.tonal.tones.size() > 8) return false;
        return true;
    }
    static MosaicGpuTextAtlas gpuTextAtlas(const MosaicSettings& s);
};
