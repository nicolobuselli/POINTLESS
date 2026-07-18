#version 440

// Procedural instanced Dot Grid content pass — fully uniform-driven.
// gl_InstanceIndex → lattice cell → image-space position (a port of
// GridGenerator's generators, KEEP IN SYNC), cell tone/colour from a
// textureLod on the linear fp16 mipmapped source (lod = log2(spacing)
// ≈ the CPU per-cell box average), and ALL parameter math — coverage/
// gamma/diameter/weight/tone pick/shape pick/jitter/localization/cull —
// mirrors DotGridRenderer::paintDots (KEEP IN SYNC). Every Dot Grid
// control, spacing and grid layout included, is then just a UBO update:
// no CPU rebake, no buffer upload.
// Rotation uses the QTransform convention: R(p) = (c·x − s·y, s·x + c·y).

layout(location = 0) in vec2 quadPos;    // fullscreen-quad vertex, -1..1
layout(location = 1) in vec2 quadUv;     // unused

layout(location = 0)      out vec2 v_local;   // px offset from centre, shape space
layout(location = 1) flat out vec4 v_color;
layout(location = 2) flat out vec3 v_srm;     // shapeId, r, cornerRadius

layout(std140, binding = 0) uniform buf {
    mat4 mvp;          // content px -> clip (ortho · clip-space correction)
    vec4 dims;         // contentW, contentH, sp, baseR
    vec4 p0;           // gamma, weight, jitter, opacity
    vec4 p1;           // cornerRadius, multiBias, shapeCount, toneCount
    vec4 p2;           // imageColors (1/0), maskCount, 0, 0
    vec4 shapeIds[2];  // up to 8 shape ids
    vec4 toneLevel[2]; // up to 8 tone levels, 0..255
    vec4 toneColor[8]; // sRGB rgb + per-tone opacity
    vec4 locFieldC[4]; // Dia, Gam, Wgt, Jit: cx, cy, rIn, rOut (content px)
    vec4 locScale;     // per-field parameter multiplier
    vec4 locOn;        // per-field enable, 1/0
    vec4 maskPts[5];   // enabled loc circles: cx, cy, rIn, rOut
    vec4 grid0;        // gridType, i0, j0, cols   (GridGpuLayout)
    vec4 grid1;        // ringN, lod, margin, 0
    vec4 gridT;        // m11, m12, m21, m22       (grid → image, QTransform)
    vec4 gridD;        // dx, dy, cx, cy
};

layout(binding = 1) uniform sampler2D srcTex;   // linear light, mipmapped

const float TAU = 6.28318530718;

float lin2s(float v)
{
    v = clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

// DotGridRenderer::cellSeed + cellJitterRands — bit-exact integer port.
vec3 cellRands(ivec2 c)
{
    uint h = uint(c.x * 73856093 ^ c.y * 19349663);
    h ^= (h >> 16); h *= 0x45d9f3bu; h ^= (h >> 16);
    vec3 rr;
    for (int i = 0; i < 3; ++i) {               // PCG-style output chain
        h = h * 747796405u + 2891336453u;
        uint w = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
        w ^= w >> 22u;
        rr[i] = float(w >> 8u) * (1.0 / 16777216.0);   // 24-bit → [0,1)
    }
    return rr;
}

// LocField::t — smoothstep falloff band of one circle.
float locT(vec4 f, vec2 p)
{
    float d = distance(p, f.xy);
    if (d <= f.z) return 1.0;
    if (d >= f.w || f.w <= f.z) return 0.0;
    float u = (f.w - d) / (f.w - f.z);
    return u * u * (3.0 - 2.0 * u);
}

// LocField::mul for field i (0 Dia, 1 Gam, 2 Wgt, 3 Jit).
float locMul(int i, vec2 p)
{
    return locOn[i] > 0.5 ? 1.0 + (locScale[i] - 1.0) * locT(locFieldC[i], p) : 1.0;
}

// LocMask::mask — union of the enabled circles, 1 when none enabled.
float maskV(vec2 p)
{
    int n = int(p2.y + 0.5);
    if (n < 1) return 1.0;
    float best = 0.0;
    for (int i = 0; i < n; ++i) best = max(best, locT(maskPts[i], p));
    return best;
}

void cull()
{
    gl_Position = vec4(-3.0, -3.0, 0.0, 1.0);   // degenerate, no fragments
    v_local = vec2(0.0); v_color = vec4(0.0); v_srm = vec3(0.0);
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
    if (type == 5) {                    // Phyllotaxis: r = c·√n, θ = n·golden
        float c  = sp * 0.8;
        float n  = float(idx);
        float th = n * 2.39996322973;   // π(3−√5)
        gp = cxy + c * sqrt(n) * vec2(cos(th), sin(th));
    } else if (type == 4) {             // Radial: rings of round(2π·r) samples
        if (idx == 0) {
            gp = cxy;                   // centre dot
        } else {
            int e = idx - 1;
            int ringN = max(1, int(grid1.x + 0.5));
            int r = e / ringN + 1;
            int k = e - (r - 1) * ringN;
            int n = max(1, int(round(TAU * float(r))));
            if (k >= n) dead = true;    // index-rectangle padding
            float th = float(k) * TAU / float(n);
            gp = cxy + float(r) * sp * vec2(cos(th), sin(th));
        }
    } else {                            // Square / Hexagonal / Brick / Wave
        int i0   = int(floor(grid0.y + 0.5));
        int j0   = int(floor(grid0.z + 0.5));
        int cols = max(1, int(grid0.w + 0.5));
        int j = j0 + idx / cols;
        int i = i0 + idx - (idx / cols) * cols;
        float vstep = (type == 1) ? sp * 0.86602540378 : sp;   // hex rows
        float xoff  = (type == 1 || type == 2) && (((j % 2) + 2) % 2 == 1)
                    ? sp * 0.5 : 0.0;                          // odd-row offset
        float gx = cxy.x + float(i) * sp + xoff;
        float gy = cxy.y + float(j) * vstep;
        if (type == 3) gy += 0.9 * sp * sin(gx * TAU / (sp * 8.0));   // wave
        gp = vec2(gx, gy);
    }

    // Grid → image space, then GridGenerator's margin cull.
    vec2 pos = vec2(gridT.x * gp.x + gridT.z * gp.y + gridD.x,
                    gridT.y * gp.x + gridT.w * gp.y + gridD.y);
    float margin = grid1.z;
    if (dead ||
        pos.x < -margin || pos.x > dims.x + margin ||
        pos.y < -margin || pos.y > dims.y + margin) { cull(); return; }

    // ── cell tone/colour: mip average of the linear source ─────────────────
    vec3  lin     = textureLod(srcTex, pos / dims.xy, grid1.y).rgb;
    float lumLin  = dot(lin, vec3(0.2126, 0.7152, 0.0722));
    float lumPerc = lin2s(lumLin);
    vec3  rands   = cellRands(ivec2(round(pos)));

    // ── parameter math (paintDots mirror) ──────────────────────────────────
    float darkness = 1.0 - lumLin;
    float invG     = 1.0 / max(0.01, p0.x * locMul(1, pos));
    float cov      = darkness <= 0.0 ? 0.0 : pow(darkness, invG);
    float locR     = dims.w * locMul(0, pos) * maskV(pos);
    if (locR * sqrt(cov) < 0.5) { cull(); return; }   // empty cell by true coverage
    float weightv = clamp(p0.y * locMul(2, pos), 0.0, 1.0);
    float covW    = cov + weightv * (1.0 - cov);
    float r       = locR * sqrt(covW);      // dot area ∝ ink coverage

    // Multi-shape pick by perceptual luminosity (same integer math as CPU).
    float shapeId = shapeIds[0].x;
    int nShapes = int(p1.z + 0.5);
    if (nShapes > 1) {
        int lumInt = int(lumPerc * 255.0);
        int adj    = clamp(lumInt + int(p1.y), 0, 255);
        int sidx   = clamp((adj * nShapes) / 256, 0, nShapes - 1);
        shapeId = shapeIds[sidx >> 2][sidx & 3];
    }

    // Colour: cell average (ImageColors) or nearest tone by level.
    vec4 col;
    int nTones = int(p1.w + 0.5);
    if (p2.x > 0.5 || nTones < 1) {
        col = vec4(lin2s(lin.r), lin2s(lin.g), lin2s(lin.b), 1.0);
    } else {
        int lum = int(round(lumPerc * 255.0));
        int best = 0;
        float bestDist = 512.0;
        for (int i = 0; i < nTones; ++i) {
            float d = abs(float(lum) - toneLevel[i >> 2][i & 3]);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        col = toneColor[best];
    }
    col.a *= p0.w;

    // Jitter: random spin about a pivot drifting off the centroid.
    // center' = s + pivot − R(pivot), shape rotated by R.
    float jit = clamp(p0.z * locMul(3, pos), 0.0, 1.0);
    float rotRad = 0.0;
    vec2  center = pos;
    if (jit > 0.0) {
        rotRad = radians((rands.x * 2.0 - 1.0) * 180.0 * jit);
        float ang = rands.y * TAU;
        float mag = rands.z * jit * sp;
        vec2 pivot = vec2(cos(ang), sin(ang)) * mag;
        float c0 = cos(rotRad), s0 = sin(rotRad);
        center = pos + pivot - vec2(pivot.x * c0 - pivot.y * s0,
                                    pivot.x * s0 + pivot.y * c0);
    }

    // Star points sit at radius r; +1.5px margin keeps the AA ramp inside.
    float pad = r + 1.5;
    vec2 local = quadPos * pad;
    float c = cos(rotRad), s = sin(rotRad);
    vec2 world = center + vec2(c * local.x - s * local.y,
                               s * local.x + c * local.y);
    v_local = local;
    v_color = col;
    v_srm   = vec3(shapeId, r, p1.x);
    gl_Position = mvp * vec4(world, 0.0, 1.0);
}
