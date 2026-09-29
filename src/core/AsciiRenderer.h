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
// Glyph atlas + measured coverages for the GPU ASCII pass. The atlas holds
// every charset glyph (white on transparent) plus the 5 effect glyphs
// (- / | \ #) appended at indices nChars..nChars+4, each rasterised in a
// padded glyphW×glyphH tile so overflowing glyphs aren't clipped.
struct AsciiGpuAtlas {
    QImage image;                    // RGBA8888 straight; glyph ink in alpha
    int    cellW = 0, cellH = 0;     // logical cell size (font metrics)
    int    glyphW = 0, glyphH = 0;   // padded atlas tile size
    int    atlasCols = 0;            // tiles per atlas row
    int    nChars = 0;               // charset glyph count (specials excluded)
    int    spaceIndex = -1;          // charset index of ' ', or -1 if absent
    std::vector<float> coverage;     // per charset index, normalised (CPU-identical)
    std::vector<int>   sortedIdx;    // charset indices ascending by coverage
};

class AsciiRenderer
{
public:
    static void render(const QImage& input, QPainter& output, const AsciiSettings& params);

    // GPU pass support: Square grid uses the fullscreen coverage-ramp pass
    // (ascii.vert/.frag, edges/hatching/contour included); non-square
    // lattices use the instanced glyph-billboard pass (ascii_grid.vert/.frag,
    // no edges/hatching/contour — same limitation as the CPU non-square
    // branch, which has no regular neighbour grid for the Sobel/isoline
    // passes). Palette fill uses OkLab on both GPU paths (up to 8 colors).
    // Braille and larger tone lists fall back to CPU.
    static bool gpuRenderable(const AsciiSettings& s);
    // True when gpuRenderable(s) should use the instanced non-square path
    // instead of the fullscreen one.
    static bool gpuInstanced(const AsciiSettings& s) { return s.gridShape != GridType::Square; }
    // Cached per (font, weight, charset, cell size); cheap on repeat calls.
    static AsciiGpuAtlas gpuAtlas(const AsciiSettings& s);
};
