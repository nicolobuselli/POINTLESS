#pragma once

#include "Params.h"
#include <vector>

// ============================================================
//  GridGenerator
//
//  Produces the set of sample positions a halftone renderer draws
//  onto, decoupling the spatial layout (square / hexagonal / radial)
//  from the primitive rendering. Global rotation and anisotropic
//  stretch are baked into the returned image-space coordinates.
// ============================================================

struct GridSample {
    float x         = 0.0f;   // image-space position
    float y         = 0.0f;
    float angle     = 0.0f;   // local tangent (radians) — Line / Circles
    int   structure = 0;      // generating structure index
};

// GPU instancing layout: enough data for dot.vert to reconstruct every
// sample position of `generate` from gl_InstanceIndex alone (KEEP IN SYNC
// with the generators below). Lattice types enumerate a [i0..i0+cols) ×
// [j0..j0+rows) index rectangle (slightly wider than the CPU's per-row
// ranges — the shader's image-space margin cull drops the extras); Radial
// uses a fixed ring × ringN rectangle (cells past each ring's true sample
// count are culled); Phyllotaxis is purely index-driven.
struct GridGpuLayout {
    float m11 = 1, m12 = 0, m21 = 0, m22 = 1, dx = 0, dy = 0;  // grid → image
    int   type  = 0;
    int   i0    = 0, j0 = 0, cols = 0, rows = 0;
    int   ringN = 0;    // Radial: index-rectangle width (max samples per ring)
    int   count = 0;    // instances to draw
};

class GridGenerator
{
public:
    // Generate samples covering the [0,imgW] × [0,imgH] image. Cached:
    // identical (settings, size) returns the previous result.
    static std::vector<GridSample> generate(const GridSettings& g, int imgW, int imgH);

    // Instancing layout for the GPU Dot Grid path (see GridGpuLayout).
    static GridGpuLayout computeGpuLayout(const GridSettings& g, int imgW, int imgH);
};
