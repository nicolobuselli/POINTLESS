#version 440

// GPU ASCII content pass — per-fragment port of AsciiRenderer::render's
// square-grid loop (KEEP IN SYNC, incl. fillAsciiParams in
// GpuCanvasWidget.cpp). Each fragment maps to its cell, samples the adjust
// chain's linear source at cell resolution (and neighbour cells for the
// Sobel/isoline effects), then picks a glyph: contour isoline gate →
// edge/hatch directional glyphs → coverage ramp. Braille and Palette stay
// CPU (gpuRenderable gates them out). The 5 special glyphs (- / | \ #) live
// at atlas indices nChars..nChars+4.
//
// Overflow: the atlas tile is 2x the cell pitch so glyphs (e.g. '@') can
// overshoot their nominal box (see gpuAtlas in AsciiRenderer.cpp). A single
// fragment's own home cell only covers the tile's centre strip, so main()
// evaluates the 2x2 block of cells that could reach this fragment and
// composites them in the CPU path's paint order (row-major) — otherwise
// overshoot ink is computed into the atlas but never sampled by anyone.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 dims;           // contentW, contentH, cellW, cellH
    vec4 grid;           // cols, rows, lod, opacity
    vec4 atlas;          // glyphW, glyphH, atlasCols, atlasTexW
    vec4 pA;             // nChars, gamma (actual), orderedDither, imageColors
    vec4 pB;             // stipple 0..100, atlasTexH, nTones, maskCount
    vec4 pC;             // edges 0..100, hatching 0..100, contour 0..100, 0
    vec4 coverage[32];   // 128 glyph coverages, normalised
    vec4 sortedIdx[32];  // 128 charset indices ascending by coverage (as float)
    vec4 toneLevel[2];   // 8 tone levels, 0..255
    vec4 toneColor[8];   // sRGB rgb + per-tone opacity
    vec4 locFieldC[5];   // AsGamma, AsEdges, AsHatching, AsStipple, AsContour
    vec4 locSO[5];       // x = scale, y = on
    vec4 maskPts[5];     // spotlight circles
};

layout(binding = 1) uniform sampler2D srcTex;     // linear light, mipmapped
layout(binding = 2) uniform sampler2D atlasTex;   // glyph atlas, alpha = ink

const vec3 W = vec3(0.2126, 0.7152, 0.0722);

float lin2s(float v)
{
    v = clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

float locT(vec4 f, vec2 p)
{
    float d = distance(p, f.xy);
    if (d <= f.z) return 1.0;
    if (d >= f.w || f.w <= f.z) return 0.0;
    float u = (f.w - d) / (f.w - f.z);
    return u * u * (3.0 - 2.0 * u);
}
float locMul(int i, vec2 p)
{
    return locSO[i].y > 0.5 ? 1.0 + (locSO[i].x - 1.0) * locT(locFieldC[i], p) : 1.0;
}
float maskV(vec2 p)
{
    int n = int(pB.w + 0.5);
    if (n < 1) return 1.0;
    float best = 0.0;
    for (int i = 0; i < n; ++i) best = max(best, locT(maskPts[i], p));
    return best;
}

float cov(int i)      { return coverage[i >> 2][i & 3]; }
int   sortedAt(int i) { return int(sortedIdx[i >> 2][i & 3] + 0.5); }

float cellNoise(int col, int row)
{
    uint h = uint(col) * 374761393u + uint(row) * 668265263u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= (h >> 16u);
    return float(h % 100000u) / 100000.0;
}

float bayerThreshold(int col, int row)
{
    const int kB[16] = int[](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    return (float(kB[(row & 3) * 4 + (col & 3)]) + 0.5) / 16.0;
}

// Perceptual luminosity of a cell (clamped to the grid), = CPU lumAt.
float lumCell(int cx, int cy)
{
    ivec2 c = clamp(ivec2(cx, cy), ivec2(0), ivec2(int(grid.x) - 1, int(grid.y) - 1));
    vec2 ctr = min((vec2(c) + 0.5) * dims.zw, dims.xy - 0.5);
    return lin2s(dot(textureLod(srcTex, ctr / dims.xy, grid.z).rgb, W));
}

void sobel(int col, int row, out float gx, out float gy)
{
    gx = lumCell(col + 1, row - 1) + 2.0 * lumCell(col + 1, row) + lumCell(col + 1, row + 1)
       - lumCell(col - 1, row - 1) - 2.0 * lumCell(col - 1, row) - lumCell(col - 1, row + 1);
    gy = lumCell(col - 1, row + 1) + 2.0 * lumCell(col, row + 1) + lumCell(col + 1, row + 1)
       - lumCell(col - 1, row - 1) - 2.0 * lumCell(col, row - 1) - lumCell(col + 1, row - 1);
}

// Direction glyph index (0 '-', 1 '/', 2 '|', 3 '\') = atlas special offset.
int edgeDirIndex(float gx, float gy)
{
    float deg = degrees(atan(-gy, gx)) + 90.0;
    deg = mod(mod(deg, 180.0) + 180.0, 180.0);
    return int(mod(float(int(deg / 45.0 + 0.5)), 4.0));
}

int bandAt(int col, int row, int bands)
{
    return int(clamp(lumCell(col, row), 0.0, 0.999) * float(bands));
}

struct CellPick { float ink; vec4 pen; };

// Full glyph pick + atlas sample for one cell (col,row), sampled at fragPx.
// fragPx may sit outside this cell's own box — that's the point: the atlas
// tile is padded to 2x cell size so overshooting glyphs (ascenders, wide
// bowls like '@') can be sampled by a neighbouring cell's fragments too.
CellPick shadeCandidate(int col, int row, vec2 fragPx, vec2 cellSz)
{
    CellPick r; r.ink = 0.0; r.pen = vec4(0.0);
    if (col < 0 || row < 0 || col >= int(grid.x) || row >= int(grid.y)) return r;

    vec2 center = min((vec2(col, row) + 0.5) * cellSz, dims.xy - 0.5);
    vec3 lin     = textureLod(srcTex, center / dims.xy, grid.z).rgb;
    float lumLin = dot(lin, W);
    float lumPerc = lin2s(lumLin);

    float gamma = pA.y * locMul(0, center);
    float invG  = 1.0 / max(0.01, gamma);
    float darkness = pow(clamp(1.0 - lumLin, 0.0, 1.0), invG);

    float stip = clamp(pB.x * locMul(3, center), 0.0, 100.0) / 100.0 * 0.5;
    if (stip > 0.0)
        darkness = clamp(darkness + (cellNoise(col, row) - 0.5) * stip, 0.0, 1.0);

    int nChars = int(pA.x + 0.5);

    // Contour isoline gate: outside a band boundary → blank cell.
    float contour = pC.z * locMul(4, center);
    if (contour > 0.5) {
        int bands = clamp(2 + int(contour / 100.0 * 30.0 + 0.5), 2, 32);
        int b = bandAt(col, row, bands);
        bool boundary = b != bandAt(col - 1, row, bands) || b != bandAt(col + 1, row, bands)
                     || b != bandAt(col, row - 1, bands) || b != bandAt(col, row + 1, bands);
        if (!boundary) return r;
    }

    // Glyph pick: edges → hatching → coverage ramp.
    int  gidx = 0;
    bool chosen = false;

    float edges = pC.x * locMul(1, center);
    if (edges > 0.5) {
        float gx, gy;
        sobel(col, row, gx, gy);
        float mag = sqrt(gx * gx + gy * gy) / 4.0;
        float thr = edges <= 0.5 ? 2.0 : 1.0 - 0.94 * edges / 100.0;
        if (mag > thr) { gidx = nChars + edgeDirIndex(gx, gy); chosen = true; }
    }

    float hatching = pC.y * locMul(2, center);
    if (!chosen && hatching > 0.5) {
        float thr = 1.0 - hatching / 100.0;
        if (darkness > thr) {
            if (darkness > min(1.0, thr + 0.3)) {
                gidx = nChars + 4;                 // '#'
            } else {
                float gx, gy;
                sobel(col, row, gx, gy);
                gidx = nChars + edgeDirIndex(gx, gy);
            }
            chosen = true;
        }
    }

    if (!chosen) {
        if (pA.z > 0.5) {
            int lo = 0, hi = nChars - 1;
            for (int it = 0; it < 8; ++it) {
                if (lo >= hi - 1) break;
                int mid = (lo + hi) / 2;
                if (cov(sortedAt(mid)) <= darkness) lo = mid; else hi = mid;
            }
            float covLo = cov(sortedAt(lo));
            float covHi = cov(sortedAt(hi));
            float span  = covHi - covLo;
            float frac  = span > 1e-4 ? clamp((darkness - covLo) / span, 0.0, 1.0) : 0.0;
            gidx = (frac > bayerThreshold(col, row)) ? sortedAt(hi) : sortedAt(lo);
        } else {
            float best = 2.0;
            for (int i = 0; i < nChars; ++i) {
                float d = abs(cov(i) - darkness);
                if (d < best) { best = d; gidx = i; }
            }
        }
    }

    // Sample that glyph tile at this cell's own offset within the padded
    // atlas tile — candCenterPx (not fragPx's home cell) is what anchors it,
    // so a neighbour's overflow lands correctly relative to fragPx.
    vec2 glyphSz  = atlas.xy;
    vec2 texSize  = vec2(atlas.w, pB.y);
    int  cols     = int(atlas.z + 0.5);
    vec2 candCenterPx = (vec2(col, row) + 0.5) * cellSz;
    vec2 aLocal   = fragPx - candCenterPx + glyphSz * 0.5;
    if (any(lessThan(aLocal, vec2(0.0))) || any(greaterThanEqual(aLocal, glyphSz))) return r;
    vec2 tileOrg = vec2(float(gidx % cols), float(gidx / cols)) * glyphSz;
    r.ink = texture(atlasTex, (tileOrg + aLocal) / texSize).a;

    // Pen colour: cell average (ImageColors) or nearest tone by luminosity.
    int nTones = int(pB.z + 0.5);
    if (pA.w > 0.5 || nTones < 1) {
        r.pen = vec4(lin2s(lin.r), lin2s(lin.g), lin2s(lin.b), 1.0);
    } else {
        int lum = int(round(lumPerc * 255.0));
        int best = 0;
        float bd = 512.0;
        for (int i = 0; i < nTones; ++i) {
            float d = abs(float(lum) - toneLevel[i >> 2][i & 3]);
            if (d < bd) { bd = d; best = i; }
        }
        r.pen = toneColor[best];
    }
    return r;
}

void main()
{
    vec2 fragPx = v_uv * dims.xy;
    vec2 cellSz = dims.zw;
    vec2 cellF  = floor(fragPx / cellSz);
    int  col    = int(cellF.x);
    int  row    = int(cellF.y);

    // The atlas tile is 2x the cell pitch, so a glyph's overflow reaches at
    // most half a cell beyond its own box — only the 2x2 block of cells
    // straddling fragPx can possibly paint here. Which half of the home cell
    // fragPx falls in picks which neighbour joins the other axis.
    vec2 fracInCell = fragPx / cellSz - cellF;
    int dx = fracInCell.x < 0.5 ? -1 : 1;
    int dy = fracInCell.y < 0.5 ? -1 : 1;
    int colLo = min(col, col + dx), colHi = max(col, col + dx);
    int rowLo = min(row, row + dy), rowHi = max(row, row + dy);

    // Composite in the same order the CPU path paints them (row-major,
    // top-to-bottom then left-to-right), so later cells' overflow correctly
    // overdraws earlier cells' in the overlap, matching AsciiRenderer::render.
    vec3 accumRGB = vec3(0.0);
    float accumA  = 0.0;
    int rows2[2] = int[](rowLo, rowHi);
    int cols2[2] = int[](colLo, colHi);
    for (int ri = 0; ri < 2; ++ri) {
        for (int ci = 0; ci < 2; ++ci) {
            CellPick cp = shadeCandidate(cols2[ci], rows2[ri], fragPx, cellSz);
            vec2 ctr = (vec2(cols2[ci], rows2[ri]) + 0.5) * cellSz;
            float a = cp.ink * cp.pen.a * maskV(ctr) * grid.w;
            accumRGB = cp.pen.rgb * a + accumRGB * (1.0 - a);
            accumA   = a + accumA * (1.0 - a);
        }
    }

    fragColor = vec4(accumRGB, accumA);   // premultiplied for the composite chain
}
