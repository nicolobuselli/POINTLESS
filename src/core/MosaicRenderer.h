#pragma once

#include "Params.h"
#include <QImage>
#include <QPainter>

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

    // GPU pass support (mosaic.vert/.frag): instanced tile fill. Tiles with
    // text labels, Palette fill (OkLab) and tonal disabled fall back to the
    // CPU (per-tone text atlas is a known TODO).
    static bool gpuRenderable(const MosaicSettings& s)
    {
        if (!s.tonal.enabled) return false;
        if (s.tonal.mode == ToneMode::Palette) return false;
        if (s.tonal.mode == ToneMode::FixedTones && s.tonal.tones.size() > 8) return false;
        for (size_t i = 0; i < s.texts.size() && i < s.tonal.tones.size(); ++i)
            if (!s.texts[i].isEmpty()) return false;   // drawable label → CPU
        return true;
    }
};
