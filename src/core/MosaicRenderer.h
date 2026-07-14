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
};
