#version 440

// Halftone (canonical AM screen) — per-pixel port of HalftoneRenderer.cpp
// (KEEP IN SYNC), which itself is modeled after paper-design/shaders'
// halftone-cmyk (Apache-2.0). Fully uniform-driven: every parameter drag is
// a UBO update, zero CPU work.
//
// The source texture is LINEAR-light RGBA16F with mips; textureLod at
// lod = log2(spacing) approximates the CPU's per-cell box average. For each
// of up to 8 rotated screens (4 CMYK inks, or one per tone) the pixel scans
// the 5×5 surrounding lattice cells, evaluates each dot analytically
// (Round / Square / Line) or accumulates the soft splat field (Ink), and
// the screens multiply over the paper colour like the CPU's Multiply pass.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 dims;      // contentW, contentH, spacing(px), lod
    vec4 pA;        // gamma, softness, gridNoise(0..1), grain
    vec4 pB;        // opacity, screenCount, dotShape (0 Round 1 Square 2 Line 3 Ink), cmyk(1/0)
    vec4 pC;        // singleTone(1/0), 0, 0, 0
    vec4 paper;     // sRGB rgb, 1
    vec4 scrA[8];   // angleDeg, flood, gain, level
    vec4 scrB[8];   // levelLo, levelHi, 0, 0
    vec4 inks[8];   // sRGB rgb + alpha (tonal: tone opacity)
};

layout(binding = 1) uniform sampler2D srcTex;   // linear light, mipmapped

const float PI = 3.14159265359;
const float kMinEdgeSoftness = 0.02;

float lin2s(float v)
{
    v = clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

// cellJitter (HalftoneRenderer.cpp): per-cell offset hash, salt-decorrelated.
vec2 cellJitter(ivec2 c, int salt, float amount)
{
    uint h = uint(c.x * 73856093 ^ c.y * 19349663 ^ (salt + 1) * 83492791);
    h ^= (h >> 16); h *= 0x45d9f3bu; h ^= (h >> 16);
    return (vec2(float(h & 0xFFFFu), float((h >> 16) & 0xFFFFu)) / 65535.0 - 0.5) * amount;
}

float noiseHash(ivec2 v)
{
    uint h = uint(v.x * 73856093 ^ v.y * 19349663);
    h ^= (h >> 16); h *= 0x45d9f3bu; h ^= (h >> 16);
    return float(h & 0xFFFFFFu) / 16777215.0;
}

float valueNoise(vec2 p)
{
    ivec2 i = ivec2(floor(p));
    vec2  f = p - vec2(i);
    f = f * f * (3.0 - 2.0 * f);
    float a = noiseHash(i),               b = noiseHash(i + ivec2(1, 0));
    float c = noiseHash(i + ivec2(0, 1)), d = noiseHash(i + ivec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

const float kGrainScale = 0.45;

// Ink coverage of screen s over the sampling cell centred on `ctr` (px).
float coverageAt(vec2 ctr, int s)
{
    vec3 c = textureLod(srcTex, ctr / dims.xy, dims.w).rgb;   // linear avg
    if (pB.w > 0.5) {                                          // CMYK separation
        float k = 1.0 - max(c.r, max(c.g, c.b));
        if (k >= 0.9999) return s == 3 ? 1.0 : 0.0;
        float inv = 1.0 / (1.0 - k);
        vec4 cmyk = vec4((1.0 - c.r - k) * inv,
                         (1.0 - c.g - k) * inv,
                         (1.0 - c.b - k) * inv, k);
        return cmyk[s];
    }
    float L = (0.2126 * lin2s(c.r) + 0.7152 * lin2s(c.g) + 0.0722 * lin2s(c.b)) * 255.0;
    if (pC.x > 0.5) return 1.0 - L / 255.0;    // single tone: classic screen
    float level = scrA[s].w, lo = scrB[s].x, hi = scrB[s].y;
    if (L <= level) {
        if (lo < -0.5) return 1.0;
        if (L <= lo)   return 0.0;
        return (L - lo) / (level - lo);
    }
    if (hi > 255.5) return 1.0;
    if (L >= hi)    return 0.0;
    return (hi - L) / (hi - level);
}

// Gamma + flood/gain curve applied to raw coverage (same order as CPU).
float shapeCov(float cov, int s)
{
    cov = pow(clamp(cov, 0.0, 1.0), 1.0 / clamp(pA.x, 0.1, 5.0));
    return clamp(cov * (1.0 + scrA[s].z) + scrA[s].y, 0.0, 1.0);
}

float aaStep(float edge, float d)   // 1 inside (d < edge), 1px AA ramp
{
    float aa = max(fwidth(d), 1e-4);
    return clamp((edge - d) / aa + 0.5, 0.0, 1.0);
}

void main()
{
    vec2  p    = v_uv * dims.xy;          // pixel centre, image px
    float sp   = dims.z;
    float soft = clamp(pA.y, 0.0, 1.0);
    float edgeSoft = max(soft, kMinEdgeSoftness);
    float gn   = clamp(pA.z, 0.0, 1.0) * sp;
    float grain = clamp(pA.w, 0.0, 1.0);
    int   nScr  = int(pB.y + 0.5);
    int   shape = int(pB.z + 0.5);
    vec2  C     = dims.xy * 0.5;

    // Feather shells for Square/Line (HalftoneRenderer's `shells`).
    const vec3 shellGrow  = vec3(0.30, 0.15, 0.0);
    const vec3 shellAlpha = vec3(0.25, 0.55, 1.0);

    vec3 color = paper.rgb;

    for (int s = 0; s < nScr; ++s) {
        float ang = radians(scrA[s].x);
        float ca = cos(ang), sa = sin(ang);
        // image → grid space (inverse of M = T(C)·R(ang)·T(−C))
        vec2 q0 = p - C;
        vec2 g  = vec2(ca * q0.x + sa * q0.y, -sa * q0.x + ca * q0.y);
        vec2 cell0 = round(g / sp);

        float inkA  = inks[s].a;
        float alive = 1.0;       // ∏(1 − a_i): SourceOver accumulation
        float field = 0.0;       // Ink style splat field

        for (int dj = -2; dj <= 2; ++dj)
        for (int di = -2; di <= 2; ++di) {
            vec2 cg = (cell0 + vec2(float(di), float(dj))) * sp;   // grid space
            vec2 ctr = C + vec2(ca * cg.x - sa * cg.y,             // image space
                                sa * cg.x + ca * cg.y);
            // GridGenerator margin cull: lattice points past image+sp don't exist.
            if (ctr.x < -sp || ctr.x > dims.x + sp ||
                ctr.y < -sp || ctr.y > dims.y + sp) continue;
            if (gn > 0.0)
                ctr += cellJitter(ivec2(round(ctr)), s, gn);
            if (distance(p, ctr) > 2.2 * sp) continue;             // outside any reach

            float cov = shapeCov(coverageAt(ctr, s), s);

            if (shape == 3) {                                      // Ink: accumulate field
                if (cov <= 0.005) continue;
                float r = 1.15 * cov * sp;
                if (r < 0.5) continue;
                float d = distance(p, ctr);
                if (d >= r) continue;
                float t = 1.0 - d / r;
                field += t * t * (3.0 - 2.0 * t);
                continue;
            }
            if (cov <= 0.001) continue;

            if (shape == 0) {                                      // Round
                float r = sp * sqrt(cov / PI);
                if (r < 0.25) continue;
                float d = distance(p, ctr);
                float a;
                if (edgeSoft <= 0.01) {
                    a = aaStep(r, d);
                } else {
                    float R  = r * (1.0 + 0.35 * edgeSoft);
                    float t0 = clamp(1.0 - 0.75 * edgeSoft, 0.0, 0.99);
                    float x  = d / R;
                    a = x <= t0 ? 1.0 : clamp((1.0 - x) / (1.0 - t0), 0.0, 1.0);
                }
                alive *= 1.0 - a * inkA;
            } else {                                               // Square / Line
                // local screen-aligned coords (shapes rotate with the lattice)
                vec2 lq = p - ctr;
                vec2 l2 = vec2(ca * lq.x + sa * lq.y, -sa * lq.x + ca * lq.y);
                if (shape == 1) {
                    float half0 = sp * sqrt(cov) * 0.5;
                    if (half0 < 0.2) continue;
                    for (int k = 0; k < 3; ++k) {
                        if (edgeSoft <= 0.01 && shellAlpha[k] < 1.0) continue;
                        float hh = half0 * (1.0 + shellGrow[k] * edgeSoft);
                        float d  = max(abs(l2.x), abs(l2.y)) - hh;
                        alive *= 1.0 - aaStep(0.0, d) * shellAlpha[k] * inkA;
                    }
                } else {                                           // Line
                    float th = sp * cov;
                    if (th < 0.2) continue;
                    for (int k = 0; k < 3; ++k) {
                        if (edgeSoft <= 0.01 && shellAlpha[k] < 1.0) continue;
                        float t2 = th * (1.0 + shellGrow[k] * edgeSoft);
                        vec2  hd = vec2(sp * 0.55, t2 * 0.5);
                        vec2  dq = abs(l2) - hd;
                        float d  = max(dq.x, dq.y);
                        alive *= 1.0 - aaStep(0.0, d) * shellAlpha[k] * inkA;
                    }
                }
            }
        }

        float a;
        if (shape == 3) {
            // Threshold over the union; grain wobbles the blob edges.
            if (grain > 0.005)
                field += (valueNoise(floor(p) * kGrainScale) - 0.5) * grain * 0.7;
            float lo = 0.5 - 0.5 * edgeSoft;
            float hi = 0.5 + 0.5 * edgeSoft + 0.01;
            float t  = clamp((field - lo) / (hi - lo), 0.0, 1.0);
            a = t * t * (3.0 - 2.0 * t) * inkA;
        } else {
            a = 1.0 - alive;
        }
        color *= mix(vec3(1.0), inks[s].rgb, a);                   // Multiply over paper
    }

    // Grain only wobbles the blob edges above (organic displacement); no
    // visible speckle overlay here — it made high grain values too noisy.

    float op = clamp(pB.x, 0.0, 1.0);
    fragColor = vec4(color * op, op);   // premultiplied, layer alpha = opacity
}
