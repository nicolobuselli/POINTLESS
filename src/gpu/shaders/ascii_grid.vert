#version 440

// Instanced ASCII content pass for NON-SQUARE lattices (Hex/Brick/Wave/
// Radial/Phyllotaxis) — one glyph billboard per GridGenerator sample,
// mirroring AsciiRenderer::render's non-square branch (KEEP IN SYNC).
// That CPU path never rotates glyphs and skips edges/hatching/contour (no
// regular neighbour grid — see the ponytail comment there), so this port
// carries only coverage ramp + stipple + gamma + tonal + loc(gamma,stipple)
// + spotlight, same as the fullscreen ascii.frag's base path. The lattice
// reconstruction from gl_InstanceIndex is the same GridGenerator port as
// dot.vert/mosaic.vert (KEEP IN SYNC ×3). All the glyph-pick math (coverage
// bracket / ordered dither / tone pick) runs HERE, per instance, so the
// fragment stage only has to sample the atlas tile.

layout(location = 0) in vec2 quadPos;    // -1..1
layout(location = 1) in vec2 quadUv;     // 0..1, v=0 at the top — atlas-tile UV

layout(location = 0)      out vec2 v_uv;      // atlas-tile-local, interpolated
layout(location = 1) flat out vec4 v_pen;     // premultiply-ready pen colour
layout(location = 2) flat out vec2 v_tileOrg; // atlas tile origin, px

layout(std140, binding = 0) uniform buf {
    mat4 mvp;          // content px -> clip
    vec4 dims;         // contentW, contentH, spacing(=cellH), lod
    vec4 p0;           // cellW, cellH, gamma (actual), opacity
    vec4 p1;           // nChars, orderedDither, imageColors, nTones
    vec4 p2;           // maskCount, atlasCols, glyphW, glyphH
    vec4 p3;           // atlasTexW, atlasTexH, spaceIndex, stipple 0..100
    vec4 coverage[32]; // 128 glyph coverages, normalised
    vec4 sortedIdx[32];// 128 charset indices ascending by coverage (as float)
    vec4 toneLevel[2]; // up to 8 tone levels, 0..255
    vec4 toneColor[8]; // sRGB rgb + per-tone opacity
    vec4 locFieldC[2]; // AsGamma, AsStipple: cx, cy, rIn, rOut (content px)
    vec4 locSO[2];     // x = scale, y = on
    vec4 maskPts[5];   // enabled loc circles
    vec4 grid0;        // gridType, i0, j0, cols   (GridGpuLayout)
    vec4 grid1;        // ringN, margin, 0, 0
    vec4 gridT;        // m11, m12, m21, m22
    vec4 gridD;        // dx, dy, cx, cy
};

layout(binding = 1) uniform sampler2D srcTex;   // linear light, mipmapped

const float TAU = 6.28318530718;
const vec3  W   = vec3(0.2126, 0.7152, 0.0722);

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
    int n = int(p2.x + 0.5);
    if (n < 1) return 1.0;
    float best = 0.0;
    for (int i = 0; i < n; ++i) best = max(best, locT(maskPts[i], p));
    return best;
}

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
float cov(int i)      { return coverage[i >> 2][i & 3]; }
int   sortedAt(int i) { return int(sortedIdx[i >> 2][i & 3] + 0.5); }

void cull()
{
    gl_Position = vec4(-3.0, -3.0, 0.0, 1.0);
    v_uv = vec2(0.0); v_pen = vec4(0.0); v_tileOrg = vec2(0.0);
}

void main()
{
    float sp   = dims.z;
    int   idx  = gl_InstanceIndex;
    int   type = int(grid0.x + 0.5);
    vec2  cxy  = gridD.zw;

    // ── gl_InstanceIndex → grid-space sample (GridGenerator port) ──────────
    vec2 gp;
    bool dead = false;
    if (type == 5) {                    // Phyllotaxis
        float c  = sp * 0.8;
        float n  = float(idx);
        float th = n * 2.39996322973;
        gp = cxy + c * sqrt(n) * vec2(cos(th), sin(th));
    } else if (type == 4) {             // Radial
        if (idx == 0) {
            gp = cxy;
        } else {
            int e = idx - 1;
            int ringN = max(1, int(grid1.x + 0.5));
            int r = e / ringN + 1;
            int k = e - (r - 1) * ringN;
            int n = max(1, int(round(TAU * float(r))));
            if (k >= n) dead = true;
            float th = float(k) * TAU / float(n);
            gp = cxy + float(r) * sp * vec2(cos(th), sin(th));
        }
    } else {                            // Square / Hexagonal / Brick / Wave
        int i0   = int(floor(grid0.y + 0.5));
        int j0   = int(floor(grid0.z + 0.5));
        int cols = max(1, int(grid0.w + 0.5));
        int j = j0 + idx / cols;
        int i = i0 + idx - (idx / cols) * cols;
        float vstep = (type == 1) ? sp * 0.86602540378 : sp;
        float xoff  = (type == 1 || type == 2) && (((j % 2) + 2) % 2 == 1)
                    ? sp * 0.5 : 0.0;
        float gx = cxy.x + float(i) * sp + xoff;
        float gy = cxy.y + float(j) * vstep;
        if (type == 3) gy += 0.9 * sp * sin(gx * TAU / (sp * 8.0));
        gp = vec2(gx, gy);
    }

    vec2 pos = vec2(gridT.x * gp.x + gridT.z * gp.y + gridD.x,
                    gridT.y * gp.x + gridT.w * gp.y + gridD.y);
    float margin = grid1.y;
    if (dead ||
        pos.x < -margin || pos.x > dims.x + margin ||
        pos.y < -margin || pos.y > dims.y + margin) { cull(); return; }

    float mv = maskV(pos);
    if (p2.x > 0.5 && mv <= 0.0) { cull(); return; }        // spotlight

    // ── cell tone + darkness (AsciiRenderer's non-square branch) ───────────
    vec3  lin     = textureLod(srcTex, pos / dims.xy, dims.w).rgb;
    float lumLin  = dot(lin, W);
    float lumPerc = lin2s(lumLin);
    float gamma   = p0.z * locMul(0, pos);
    float invG    = 1.0 / max(0.01, gamma);
    float darkness = pow(clamp(1.0 - lumLin, 0.0, 1.0), invG);

    float cellW = p0.x, cellH = p0.y;
    int col = int(pos.x / cellW);
    int row = int(pos.y / cellH);
    float stippleAmt = clamp(p3.w * locMul(1, pos), 0.0, 100.0) / 100.0 * 0.5;
    if (stippleAmt > 0.0)
        darkness = clamp(darkness + (cellNoise(col, row) - 0.5) * stippleAmt, 0.0, 1.0);

    // ── glyph pick (chooseGlyph mirror) ─────────────────────────────────────
    int nChars = int(p1.x + 0.5);
    int gidx = 0;
    if (p1.y > 0.5) {
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
    if (gidx == int(p3.z + 0.5)) { cull(); return; }   // literal space glyph

    // ── pen colour (tc.pen mirror) ──────────────────────────────────────────
    vec4 pen;
    int nTones = int(p1.w + 0.5);
    if (p1.z > 0.5 || nTones < 1) {
        pen = vec4(lin2s(lin.r), lin2s(lin.g), lin2s(lin.b), 1.0);
    } else {
        int lum = int(round(lumPerc * 255.0));
        int best = 0;
        float bd = 512.0;
        for (int i = 0; i < nTones; ++i) {
            float d = abs(float(lum) - toneLevel[i >> 2][i & 3]);
            if (d < bd) { bd = d; best = i; }
        }
        pen = toneColor[best];
    }
    pen.a *= mv * p0.w;

    // ── billboard geometry: quad = the glyph's padded atlas tile ───────────
    vec2 pad = vec2(p2.z, p2.w) * 0.5;   // glyphW/2, glyphH/2 (= cellW, cellH)
    vec2 world = pos + quadPos * pad;

    v_uv      = quadUv;
    v_pen     = pen;
    v_tileOrg = vec2(mod(float(gidx), p2.y), floor(float(gidx) / p2.y)) * vec2(p2.z, p2.w);
    gl_Position = mvp * vec4(world, 0.0, 1.0);
}
