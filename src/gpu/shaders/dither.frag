#version 440

// GPU dither content pass — ordered (threshold matrix / line hatch) and
// hard Threshold, per-pixel port of DitherRenderer::renderOrdered and
// renderThreshold (KEEP IN SYNC, incl. fillDitherParams in
// GpuCanvasWidget.cpp). Error diffusion stays CPU (serial).
//
// The chunky-pixel grid mirrors the CPU exactly: the CPU downscales the
// working image to floor(size/ps) cells, dithers there, and upscales
// nearest — here each output pixel maps to its cell (uniform stretch) and
// the cell tone is a textureLod average of the linear fp16 source (the
// adjust chain's output). FixedTones thresholds in gamma space, Threshold
// and ImageColors quantize in linear (same spaces as the CPU).

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 dims;           // contentW, contentH, cellsW, cellsH
    vec4 pA;             // lod, strength 0..1, threshold 0..1, opacity
    vec4 pB;             // algoClass (0 mask, 1 lineHatch, 2 threshold), imageColors, nTones, L
    vec4 pC;             // lineAngle deg, lineSpacing, maskW, maskH
    vec4 pD;             // spotlight mask count, 0, 0, 0
    vec4 toneColor[64];  // sRGB rgb + per-tone alpha (sorted + expanded)
    vec4 toneLevel[16];  // 64 packed levels, 0..255
    vec4 locFieldC[5];   // Str, Thr, Lvl, Ang, Spc: cx, cy, rIn, rOut (cell coords)
    vec4 locSO[5];       // x = scale, y = on
    vec4 maskPts[5];     // spotlight circles, cell coords
};

layout(binding = 1) uniform sampler2D srcTex;    // linear light, mipmapped
layout(binding = 2) uniform sampler2D maskTex;   // R32F threshold matrix (tiled)

float lin2s(float v)
{
    v = clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
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

// LocField::mul for field i (0 Str, 1 Thr, 2 Lvl, 3 Ang, 4 Spc).
float locMul(int i, vec2 p)
{
    return locSO[i].y > 0.5 ? 1.0 + (locSO[i].x - 1.0) * locT(locFieldC[i], p) : 1.0;
}

// LocMask::mask — union of the enabled circles, 1 when none enabled.
float maskV(vec2 p)
{
    int n = int(pD.x + 0.5);
    if (n < 1) return 1.0;
    float best = 0.0;
    for (int i = 0; i < n; ++i) best = max(best, locT(maskPts[i], p));
    return best;
}

float lvlAt(int i) { return toneLevel[i >> 2][i & 3]; }

void main()
{
    // Output pixel → cell (uniform stretch, = CPU's nearest upscale).
    vec2 cell = min(floor(v_uv * dims.zw), dims.zw - 1.0);
    vec3 lin  = textureLod(srcTex, (cell + 0.5) / dims.zw, pA.x).rgb;

    int  algoClass   = int(pB.x + 0.5);
    bool imageColors = pB.y > 0.5;
    vec4 col = vec4(0.0);

    if (algoClass == 2) {
        // Threshold: hard cut on LINEAR luminance (renderThreshold).
        float lum = dot(lin, vec3(0.2126, 0.7152, 0.0722));
        float t   = locSO[1].y > 0.5 ? clamp(pA.z * locMul(1, cell), 0.0, 1.0) : pA.z;
        if (imageColors)
            col = (lum < t) ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(1.0);
        else if (lum < t)
            col = toneColor[0];                        // ink (darkest tone)
    } else {
        // Ordered: threshold from the matrix or the analytic line screen.
        float t0;
        if (algoClass == 1) {
            float a   = radians(pC.x * locMul(3, cell));
            float spc = max(1.0, pC.y * locMul(4, cell));
            float u   = (cell.x * cos(a) + cell.y * sin(a)) / spc;
            float f   = u - floor(u);
            t0 = abs(f - 0.5) * 2.0;
        } else {
            ivec2 mc = ivec2(mod(cell, pC.zw));
            t0 = texelFetch(maskTex, mc, 0).r;
        }
        float str = clamp(pA.y * locMul(0, cell), 0.0, 1.0);
        float t   = 0.5 + (t0 - 0.5) * str;

        if (imageColors) {
            // Per-channel quantization in LINEAR light, L levels.
            float L = pB.w;
            if (locSO[2].y > 0.5)
                L = clamp(round(pB.w * locMul(2, cell)), 2.0, 16.0);
            vec3 val  = clamp(lin, 0.0, 1.0) * (L - 1.0);
            vec3 base = floor(val);
            vec3 q    = clamp(base + vec3(greaterThan(val - base, vec3(t))),
                              vec3(0.0), vec3(L - 1.0)) / (L - 1.0);
            col = vec4(lin2s(q.r), lin2s(q.g), lin2s(q.b), 1.0);
        } else {
            // FixedTones: gamma-encoded channels (traditional dither tools).
            vec3  gam = vec3(lin2s(lin.r), lin2s(lin.g), lin2s(lin.b));
            float lum = dot(gam, vec3(0.2126, 0.7152, 0.0722));
            int   nT  = int(pB.z + 0.5);
            if (nT <= 1) {
                if (lum < t) col = toneColor[0];       // ink on transparent paper
            } else {
                float lum255 = lum * 255.0;
                int seg = 0;
                for (int i = 0; i < 62; ++i)
                    if (seg < nT - 2 && lum255 > lvlAt(seg + 1)) ++seg;
                float l0   = lvlAt(seg);
                float l1   = lvlAt(seg + 1);
                float span = l1 - l0;
                float frac = (span > 0.5) ? (lum255 - l0) / span : 0.5;
                col = (frac > t) ? toneColor[seg + 1] : toneColor[seg];
            }
        }
    }

    // Spotlight localization mask, then layer opacity — same order as render().
    float a = col.a * maskV(cell) * pA.w;
    fragColor = vec4(col.rgb * a, a);   // premultiplied for the composite chain
}
