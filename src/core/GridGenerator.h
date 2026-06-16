#pragma once

#include "Params.h"
#include <vector>

// ============================================================
//  GridGenerator
//
//  Produces the set of sample positions a halftone renderer draws
//  onto, decoupling the spatial layout (square / hexagonal / radial /
//  line / circles) from the primitive rendering. Global rotation and
//  anisotropic stretch are baked into the returned image-space
//  coordinates.
//
//  Samples carry a `structure` index (row / line / ring) so the
//  renderer can group them — dots ignore it, while Line and Circles
//  connect consecutive samples of the same structure into strokes.
//  `angle` is the local tangent direction (radians) for those strokes.
// ============================================================

struct GridSample {
    float x         = 0.0f;   // image-space position
    float y         = 0.0f;
    float angle     = 0.0f;   // local tangent (radians) — Line / Circles
    int   structure = 0;      // generating structure index
};

class GridGenerator
{
public:
    // Generate samples covering the [0,imgW] × [0,imgH] image. Cached:
    // identical (settings, size) returns the previous result.
    static std::vector<GridSample> generate(const GridSettings& g, int imgW, int imgH);
};
