#version 440

// GPU whole-image filter pass — one pipeline covering Thermal, Blueprint,
// Metal Emboss and Glitch (Datamosh + CRT; Pixel Sort stays CPU, it's a
// data-dependent sort, not a per-pixel op). Op switch on pA.x, several
// draws per layer per frame (see GpuCanvasWidget's FilterRes plan):
//   0/1 BLUR_H/V        — Thermal's optional pre-blur (linear, separable box)
//   2   THERMAL_FILL    — Iron ramp / Fill by luminance
//   3   SOBEL_MAG        — Blueprint's edge magnitude (writes R channel)
//   4/5 DILATE_H/V       — Blueprint's line-width box-max dilation
//   6   BLUEPRINT_FILL   — threshold + Fill over paper
//   7   METAL_FILL        — Sobel bump lighting (Blinn-Phong) + Fill
//   8   GLITCH_DATAMOSH   — per-band wrap-shift + RGB channel split + Fill
//   9   GLITCH_CRT        — scanline + aperture-grille mask + Fill
//
// KEEP IN SYNC with ThermalRenderer/BlueprintRenderer/MetalEmbossRenderer/
// GlitchRenderer (CPU) and fillFilterParams/planFilterPasses in
// GpuCanvasWidget.cpp. No localization on these five modes yet (CLAUDE.md
// §10) — no loc fields in the UBO.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 dims;            // contentW, contentH, texelW, texelH
    vec4 pA;              // op, opacity, p1, p2
    vec4 pB;              // p3, p4, p5, p6
    vec4 pC;              // p7, p8, p9, p10
    vec4 pD;              // fill colour (paper / metal), rgba 0..1
    vec4 pE;              // imageColors, nTones, 0, 0
    vec4 toneColor[8];    // sRGB rgb + per-tone alpha
    vec4 toneLevel[2];    // 8 packed levels, 0..255
};

layout(binding = 1) uniform sampler2D texA;   // primary source (linear light unless noted)
layout(binding = 2) uniform sampler2D texB;   // secondary source (Blueprint's dilated magnitude)

float lin2s(float v)
{
    v = clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

vec3 lin2sVec(vec3 c) { return vec3(lin2s(c.r), lin2s(c.g), lin2s(c.b)); }

// Perceptual (gamma-encoded) weighted luma of a linear-light sample, 0..255 —
// matches ColorMath::perceptualLumaFromLinear*255.
float lum255FromLinear(vec3 lin)
{
    return dot(lin2sVec(lin) * 255.0, vec3(0.2126, 0.7152, 0.0722));
}

float lumAt(vec2 offsetTexels)
{
    return lum255FromLinear(texture(texA, v_uv + offsetTexels * dims.zw).rgb);
}

// Nearest FixedTones entry by level (Params.h pickToneIndex, ported 1:1).
vec4 pickTone(float lum255)
{
    int n = int(pE.y + 0.5);
    if (n < 1) return vec4(0.0, 0.0, 0.0, 1.0);
    int best = 0;
    float bestD = 1e9;
    for (int i = 0; i < n; ++i) {
        float lvl = toneLevel[i >> 2][i & 3];
        float d = abs(lum255 - lvl);
        if (d < bestD) { bestD = d; best = i; }
    }
    return toneColor[best];
}

// Classic FLIR "Iron" ramp (ThermalRenderer::ironColor, ported 1:1).
vec3 ironColor(float t)
{
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.0);
    vec3 c1 = vec3(40.0, 0.0, 90.0) / 255.0;
    vec3 c2 = vec3(140.0, 0.0, 110.0) / 255.0;
    vec3 c3 = vec3(220.0, 50.0, 0.0) / 255.0;
    vec3 c4 = vec3(250.0, 150.0, 0.0) / 255.0;
    vec3 c5 = vec3(255.0, 220.0, 50.0) / 255.0;
    vec3 c6 = vec3(1.0);
    if (t <= 0.15) return mix(c0, c1, (t - 0.00) / 0.15);
    if (t <= 0.35) return mix(c1, c2, (t - 0.15) / 0.20);
    if (t <= 0.55) return mix(c2, c3, (t - 0.35) / 0.20);
    if (t <= 0.75) return mix(c3, c4, (t - 0.55) / 0.20);
    if (t <= 0.90) return mix(c4, c5, (t - 0.75) / 0.15);
    return mix(c5, c6, (t - 0.90) / 0.10);
}

// GlitchRenderer::render's shared Fill tail (imageColors passthrough, else
// nearest FixedTones by the shaded pixel's own luma) + opacity.
vec4 glitchFill(vec3 gam, float srcAlpha)
{
    vec3 outc; float outa;
    if (pE.x > 0.5) {
        outc = gam; outa = srcAlpha;
    } else {
        float l255 = dot(gam * 255.0, vec3(0.2126, 0.7152, 0.0722));
        vec4 t = pickTone(l255);
        outc = t.rgb; outa = srcAlpha * t.a;
    }
    float a = outa * pA.y;
    return vec4(outc * a, a);
}

uint hashInt(uint x)
{
    x ^= x >> 16u; x *= 0x7feb352du;
    x ^= x >> 15u; x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

void main()
{
    int op = int(pA.x + 0.5);

    // --- Thermal pre-blur: separable box blur in linear light ------------
    if (op == 0 || op == 1) {
        int r = int(pA.z + 0.5);
        vec2 dir = (op == 0) ? vec2(dims.z, 0.0) : vec2(0.0, dims.w);
        vec4 sum = vec4(0.0);
        int cnt = 0;
        for (int i = -20; i <= 20; ++i) {
            if (i < -r || i > r) continue;
            sum += texture(texA, v_uv + dir * float(i));
            ++cnt;
        }
        fragColor = sum / float(max(cnt, 1));
        return;
    }

    // --- Thermal fill: false-colour remap by luminance --------------------
    if (op == 2) {
        vec4 s = texture(texA, v_uv);
        float lum255 = lum255FromLinear(s.rgb);
        float gain = pA.z, bias = pA.w;
        float t = clamp((lum255 / 255.0 - 0.5) * gain + 0.5 + bias, 0.0, 1.0);
        vec3 fillRgb; float fillA;
        if (pE.x > 0.5) { fillRgb = ironColor(t); fillA = 1.0; }
        else { vec4 tone = pickTone(t * 255.0); fillRgb = tone.rgb; fillA = tone.a; }
        float a = s.a * fillA * pA.y;
        fragColor = vec4(fillRgb * a, a);
        return;
    }

    // --- Blueprint: Sobel magnitude -> R channel ---------------------------
    if (op == 3) {
        float tl = lumAt(vec2(-1, -1)), t = lumAt(vec2(0, -1)), tr = lumAt(vec2(1, -1));
        float l  = lumAt(vec2(-1,  0)),                          r  = lumAt(vec2(1,  0));
        float bl = lumAt(vec2(-1,  1)), b = lumAt(vec2(0,  1)), br = lumAt(vec2(1,  1));
        float gx = (tr + 2.0 * r + br) - (tl + 2.0 * l + bl);
        float gy = (bl + 2.0 * b + br) - (tl + 2.0 * t + tr);
        fragColor = vec4(sqrt(gx * gx + gy * gy), 0.0, 0.0, 1.0);
        return;
    }

    // --- Blueprint: box-max dilation (line width), separable --------------
    if (op == 4 || op == 5) {
        int r = int(pA.z + 0.5);
        vec2 dir = (op == 4) ? vec2(dims.z, 0.0) : vec2(0.0, dims.w);
        float m = 0.0;
        for (int i = -4; i <= 4; ++i) {
            if (i < -r || i > r) continue;
            m = max(m, texture(texA, v_uv + dir * float(i)).r);
        }
        fragColor = vec4(m, 0.0, 0.0, 1.0);
        return;
    }

    // --- Blueprint: threshold + fill over paper ----------------------------
    if (op == 6) {
        vec4 s = texture(texA, v_uv);
        ivec2 px = ivec2(floor(v_uv * dims.xy));
        float m = texelFetch(texB, clamp(px, ivec2(0), ivec2(dims.xy) - 1), 0).r;
        float threshold = pA.z;
        vec3 fillRgb; float fillA;
        if (m > threshold) {
            if (pE.x > 0.5) { fillRgb = lin2sVec(s.rgb); fillA = s.a; }
            else { vec4 tone = pickTone(clamp(m / 255.0, 0.0, 1.0) * 255.0); fillRgb = tone.rgb; fillA = tone.a; }
        } else {
            fillRgb = pD.rgb; fillA = pD.a;
        }
        float a = fillA * pA.y;
        fragColor = vec4(fillRgb * a, a);
        return;
    }

    // --- Metal Emboss: Sobel bump-mapped Blinn-Phong lighting --------------
    if (op == 7) {
        vec4 s = texture(texA, v_uv);
        float depth = pA.z, specAmt = pA.w;
        float tl = lumAt(vec2(-1, -1)) / 255.0, t = lumAt(vec2(0, -1)) / 255.0, tr = lumAt(vec2(1, -1)) / 255.0;
        float l  = lumAt(vec2(-1,  0)) / 255.0,                                 r  = lumAt(vec2(1,  0)) / 255.0;
        float bl = lumAt(vec2(-1,  1)) / 255.0, b = lumAt(vec2(0,  1)) / 255.0, br = lumAt(vec2(1,  1)) / 255.0;
        float gx = ((tr + 2.0 * r + br) - (tl + 2.0 * l + bl)) * depth;
        float gy = ((bl + 2.0 * b + br) - (tl + 2.0 * t + tr)) * depth;
        vec3 n = normalize(vec3(-gx, -gy, 1.0));
        vec3 lgt = normalize(pB.xyz);
        float diffuse = max(0.0, dot(n, lgt));
        vec3 h = normalize(vec3(lgt.xy, lgt.z + 1.0));
        float ndoth = max(0.0, dot(n, h));
        float spec = pow(ndoth, 24.0) * specAmt;

        vec3 fillRgb; float fillA;
        if (pE.x > 0.5) {
            fillRgb = clamp(pD.rgb * diffuse + vec3(spec), 0.0, 1.0);
            fillA = 1.0;
        } else {
            vec4 tone = pickTone(diffuse * 255.0);
            fillRgb = clamp(tone.rgb + (vec3(1.0) - tone.rgb) * spec, 0.0, 1.0);
            fillA = tone.a;
        }
        float a = s.a * fillA * pA.y;
        fragColor = vec4(fillRgb * a, a);
        return;
    }

    // --- Glitch: Datamosh (per-band wrap-shift + RGB channel split) -------
    if (op == 8) {
        float amount = pA.z, channelShift = pA.w;
        float blockSizePx = max(1.0, pB.x);
        uint seedBits = floatBitsToUint(pB.y);
        int y = int(floor(v_uv.y * dims.y));
        int x = int(floor(v_uv.x * dims.x));
        int band = y / int(blockSizePx);
        uint hv = hashInt(seedBits ^ (uint(band) * 0x9e3779b9u));
        float r1 = float(hv & 0xFFFFu) / 65535.0;
        float r2 = float((hv >> 16u) & 0xFFFFu) / 65535.0;
        float dxf = 0.0;
        // qRound rounds half away from zero — floor(x+0.5) alone only matches
        // that for x >= 0, so mirror the sign explicitly for negative shifts.
        if (r1 < amount) {
            float raw = (r2 * 2.0 - 1.0) * amount * dims.x * 0.15;
            dxf = sign(raw) * floor(abs(raw) + 0.5);
        }

        int shiftPx = int(channelShift + 0.5);   // qRound, matches the CPU's pre-rounded int
        int xr = clamp(x - shiftPx, 0, int(dims.x) - 1);
        int xb = clamp(x + shiftPx, 0, int(dims.x) - 1);

        float sxr = mod(float(xr) - dxf, dims.x);
        float sxg = mod(float(x)  - dxf, dims.x);
        float sxb = mod(float(xb) - dxf, dims.x);

        vec4 pr = texture(texA, vec2((sxr + 0.5) * dims.z, v_uv.y));
        vec4 pg = texture(texA, vec2((sxg + 0.5) * dims.z, v_uv.y));
        vec4 pb = texture(texA, vec2((sxb + 0.5) * dims.z, v_uv.y));

        vec3 gam = vec3(lin2s(pr.r), lin2s(pg.g), lin2s(pb.b));
        fragColor = glitchFill(gam, pg.a);
        return;
    }

    // --- Glitch: CRT/Scanline -----------------------------------------------
    if (op == 9) {
        int y = int(floor(v_uv.y * dims.y));
        int x = int(floor(v_uv.x * dims.x));
        int spacing = max(1, int(pA.z));
        float intensity = pA.w;
        float maskStrength = pB.x;

        bool darkBand = (y % (spacing * 2)) >= spacing;
        float lineFactor = darkBand ? (1.0 - intensity) : 1.0;
        float mr = 1.0, mg = 1.0, mb = 1.0;
        int m3 = x % 3;
        if (m3 == 0) { mg = 1.0 - maskStrength; mb = 1.0 - maskStrength; }
        else if (m3 == 1) { mr = 1.0 - maskStrength; mb = 1.0 - maskStrength; }
        else { mr = 1.0 - maskStrength; mg = 1.0 - maskStrength; }

        vec4 s = texture(texA, v_uv);
        vec3 gam = lin2sVec(s.rgb) * lineFactor * vec3(mr, mg, mb);
        fragColor = glitchFill(clamp(gam, 0.0, 1.0), s.a);
        return;
    }

    fragColor = vec4(0.0);
}
