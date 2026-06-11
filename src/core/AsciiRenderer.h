#pragma once

#include "Params.h"
#include <QImage>
#include <QPainter>

/**
 * AsciiRenderer
 *
 * Maps cell luminosity onto a character ramp (light → dark) and paints
 * the glyphs with a monospace font. Painter-based so SVG export keeps
 * the text as vectors. Colors follow the tonal settings (image colors,
 * single tone or multi-tone bands). Blank (lightest) cells are left
 * transparent so the background shows through.
 */
class AsciiRenderer
{
public:
    static void render(const QImage& input, QPainter& output, const AsciiSettings& params);
};
