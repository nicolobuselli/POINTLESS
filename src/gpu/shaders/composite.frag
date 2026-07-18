#version 440

// One compositing step: dst = blend(dst, placed layer). Fullscreen pass —
// pixels outside the layer's placed quad read src alpha 0 and pass dst
// through. Faithful GLSL port of BlendCompositor.cpp: straight-alpha W3C
// formula Co = as(1-ab)Cs + as*ab*B(Cb,Cs) + (1-as)ab*Cb, with the same
// special cases (Dissolve hash dither, LinearDodge = additive Plus).

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4  mvp;
    mat4  invPlacement;   // frame px -> layer px
    vec4  sizes;          // frameW, frameH, layerW, layerH
    ivec4 modeFlags;      // x = BlendMode (Params.h enum order), y = gpuAdjust
    vec4  adjA;           // brightness -100..100, contrast -100..100, gamma (actual), saturation 0..200 (100 = neutral)
    vec4  adjB;           // levelsBlack 0..255, levelsMid (actual), levelsWhite 0..255, grain amp (0..1 units)
    vec4  adjC;           // posterize levels (256 = off), threshold 0..255 (0 = off), invert 0/1, unused
};

layout(binding = 1) uniform sampler2D dstTex;   // accumulated composite
layout(binding = 2) uniform sampler2D srcTex;   // layer raster (premultiplied)

// BlendMode enum, same order as Params.h
const int M_Normal = 0,  M_Dissolve = 1,
          M_Darken = 2,  M_Multiply = 3,  M_ColorBurn = 4, M_LinearBurn = 5, M_DarkerColor = 6,
          M_Lighten = 7, M_Screen = 8,    M_ColorDodge = 9, M_LinearDodge = 10, M_LighterColor = 11,
          M_Overlay = 12, M_SoftLight = 13, M_HardLight = 14, M_VividLight = 15,
          M_LinearLight = 16, M_PinLight = 17, M_HardMix = 18,
          M_Difference = 19, M_Exclusion = 20, M_Subtract = 21, M_Divide = 22,
          M_Hue = 23, M_Saturation = 24, M_Color = 25, M_Luminosity = 26;

float colorBurn(float b, float s)
{
    if (b >= 1.0) return 1.0;
    if (s <= 0.0) return 0.0;
    return 1.0 - min(1.0, (1.0 - b) / s);
}
float colorDodge(float b, float s)
{
    if (b <= 0.0) return 0.0;
    if (s >= 1.0) return 1.0;
    return min(1.0, b / (1.0 - s));
}
float vividLight(float b, float s)
{
    return (s <= 0.5) ? colorBurn(b, 2.0 * s) : colorDodge(b, 2.0 * s - 1.0);
}
float softLightD(float b)
{
    return (b <= 0.25) ? ((16.0 * b - 12.0) * b + 4.0) * b : sqrt(b);
}
float softLight(float b, float s)
{
    return (s <= 0.5) ? b - (1.0 - 2.0 * s) * b * (1.0 - b)
                      : b + (2.0 * s - 1.0) * (softLightD(b) - b);
}

float blendSeparable(float b, float s, int m)
{
    if (m == M_Darken)      return min(b, s);
    if (m == M_Multiply)    return b * s;
    if (m == M_ColorBurn)   return colorBurn(b, s);
    if (m == M_LinearBurn)  return max(0.0, b + s - 1.0);
    if (m == M_Lighten)     return max(b, s);
    if (m == M_Screen)      return b + s - b * s;
    if (m == M_ColorDodge)  return colorDodge(b, s);
    if (m == M_Overlay)     return (b <= 0.5) ? 2.0 * b * s : 1.0 - 2.0 * (1.0 - b) * (1.0 - s);
    if (m == M_SoftLight)   return softLight(b, s);
    if (m == M_HardLight)   return (s <= 0.5) ? 2.0 * b * s : 1.0 - 2.0 * (1.0 - b) * (1.0 - s);
    if (m == M_VividLight)  return vividLight(b, s);
    if (m == M_LinearLight) return clamp(b + 2.0 * s - 1.0, 0.0, 1.0);
    if (m == M_PinLight)    return (s <= 0.5) ? min(b, 2.0 * s) : max(b, 2.0 * s - 1.0);
    if (m == M_HardMix)     return (vividLight(b, s) < 0.5) ? 0.0 : 1.0;
    if (m == M_Difference)  return abs(b - s);
    if (m == M_Exclusion)   return b + s - 2.0 * b * s;
    if (m == M_Subtract)    return max(0.0, b - s);
    if (m == M_Divide)      return (s <= 0.0) ? 1.0 : min(1.0, b / s);
    return s;   // Normal
}

// ── Non-separable helpers (PDF spec, mirrors BlendCompositor) ──
float lum3(vec3 c) { return 0.3 * c.r + 0.59 * c.g + 0.11 * c.b; }

vec3 clipColor3(vec3 c)
{
    float l = lum3(c);
    float n = min(c.r, min(c.g, c.b));
    float x = max(c.r, max(c.g, c.b));
    if (n < 0.0) c = l + (c - l) * l / (l - n);
    if (x > 1.0) c = l + (c - l) * (1.0 - l) / (x - l);
    return c;
}
vec3 setLum3(vec3 c, float l)
{
    return clipColor3(c + (l - lum3(c)));
}
float sat3(vec3 c)
{
    return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
}
vec3 setSat3(vec3 c, float s)
{
    float mn = min(c.r, min(c.g, c.b));
    float mx = max(c.r, max(c.g, c.b));
    vec3 outc = vec3(0.0);
    if (mx > mn) {
        outc = (c - mn) * s / (mx - mn);
        // components already ordered per channel by the formula above
    }
    return outc;
}

vec3 blendNonSeparable(vec3 cb, vec3 cs, int m)
{
    if (m == M_Hue)          return setLum3(setSat3(cs, sat3(cb)), lum3(cb));
    if (m == M_Saturation)   return setLum3(setSat3(cb, sat3(cs)), lum3(cb));
    if (m == M_Color)        return setLum3(cs, lum3(cb));
    if (m == M_Luminosity)   return setLum3(cb, lum3(cs));
    if (m == M_DarkerColor)  return (lum3(cs) < lum3(cb)) ? cs : cb;
    if (m == M_LighterColor) return (lum3(cs) > lum3(cb)) ? cs : cb;
    return cs;
}

// Same per-pixel hash as BlendCompositor::hashRand (Dissolve).
float hashRand(uvec2 p)
{
    uint h = p.x * 73856093u ^ p.y * 19349663u;
    h = (h ^ (h >> 13)) * 0x5BD1E995u;
    return float(h & 0xFFFFu) / 65536.0;
}

// ── Phase 2: point-op adjustments (port of ImageAdjuster.cpp) ──
// Applied to Original layers in straight alpha, same op order as
// ImageAdjuster::apply. Neighborhood ops (blur/sharpen/edge) never reach
// here — such layers arrive CPU-adjusted with gpuAdjust off.

// ImageAdjuster's hash2d (grain).
uint hashAdj(uvec2 p)
{
    uint h = p.x * 73856093u ^ p.y * 19349663u;
    h ^= (h >> 16);
    h *= 0x45d9f3bu;
    h ^= (h >> 16);
    return h;
}

vec3 applyAdjust(vec3 c, vec2 layerPx)
{
    // Brightness / contrast (0..255-domain formula, normalized)
    if (adjA.x != 0.0 || adjA.y != 0.0) {
        float ct = adjA.y * 2.55;
        float factor = (259.0 * (ct + 255.0)) / (255.0 * (259.0 - ct));
        float off = adjA.x * 1.275 / 255.0;
        c = clamp(factor * (c - 128.0 / 255.0) + 128.0 / 255.0 + off, 0.0, 1.0);
    }
    // Gamma
    if (adjA.z != 1.0)
        c = pow(c, vec3(adjA.z));
    // Levels
    if (adjB.x != 0.0 || adjB.y != 1.0 || adjB.z != 255.0) {
        float range = adjB.z - adjB.x;
        vec3 v = (range > 0.0) ? (c * 255.0 - adjB.x) / range : vec3(0.0);
        v = clamp(v, 0.0, 1.0);
        if (adjB.y != 1.0) v = pow(v, vec3(1.0 / adjB.y));
        c = v;
    }
    // Saturation (integer-LUT luma weights 54/183/19 >> 8)
    if (adjA.w != 100.0) {
        float gray = dot(c, vec3(54.0, 183.0, 19.0) / 256.0);
        c = clamp(gray + (c - gray) * adjA.w / 100.0, 0.0, 1.0);
    }
    // Invert
    if (adjC.z > 0.5)
        c = 1.0 - c;
    // Grain (hash2d on layer-raster pixel coords)
    if (adjB.w > 0.0) {
        uint h = hashAdj(uvec2(max(vec2(0.0), floor(layerPx))));
        float n = (float(h & 0xFFFFu) / 65535.0 * 2.0 - 1.0) * adjB.w;
        c = clamp(c + n, 0.0, 1.0);
    }
    // Posterize
    if (adjC.x < 256.0) {
        float lv = max(1.0, adjC.x - 1.0);
        c = clamp(round(c * lv) / lv, 0.0, 1.0);
    }
    // Threshold
    if (adjC.y > 0.0) {
        float l = dot(c, vec3(54.0, 183.0, 19.0) / 256.0) * 255.0;
        c = vec3((l >= adjC.y) ? 1.0 : 0.0);
    }
    return c;
}

void main()
{
    vec2 framePx = v_uv * sizes.xy;
    vec4 dstP = texture(dstTex, v_uv);

    // Map this frame pixel into the layer's raster; outside = transparent.
    vec3 lp = (invPlacement * vec4(framePx, 0.0, 1.0)).xyz;
    vec2 luv = lp.xy / sizes.zw;
    vec4 srcP = vec4(0.0);
    if (luv.x >= 0.0 && luv.x <= 1.0 && luv.y >= 0.0 && luv.y <= 1.0)
        srcP = texture(srcTex, luv);

    // Phase 2: adjust the source in straight alpha before any blend math
    // (LinearDodge/Dissolve below consume srcP directly).
    if (modeFlags.y == 1 && srcP.a > 0.0) {
        vec3 ac = applyAdjust(srcP.rgb / srcP.a, lp.xy);
        srcP = vec4(ac * srcP.a, srcP.a);
    }

    int mode = modeFlags.x;
    float sa = srcP.a;
    if (sa <= 0.0) { fragColor = dstP; return; }

    if (mode == M_LinearDodge) {           // QPainter Plus: premultiplied add
        fragColor = min(dstP + srcP, vec4(1.0));
        return;
    }

    float ba = dstP.a;
    vec3 cs = srcP.rgb / max(sa, 1e-5);    // unpremultiply
    vec3 cb = (ba > 0.0) ? dstP.rgb / ba : vec3(0.0);

    if (mode == M_Dissolve) {
        fragColor = (hashRand(uvec2(framePx)) < sa) ? vec4(cs, 1.0) : dstP;
        return;
    }

    vec3 mixed;
    if (mode == M_Hue || mode == M_Saturation || mode == M_Color
     || mode == M_Luminosity || mode == M_DarkerColor || mode == M_LighterColor) {
        mixed = blendNonSeparable(cb, cs, mode);
    } else {
        mixed = vec3(blendSeparable(cb.r, cs.r, mode),
                     blendSeparable(cb.g, cs.g, mode),
                     blendSeparable(cb.b, cs.b, mode));
    }

    float ao = sa + ba * (1.0 - sa);
    if (ao <= 0.0) { fragColor = vec4(0.0); return; }
    vec3 co = sa * (1.0 - ba) * cs + sa * ba * mixed + (1.0 - sa) * ba * cb;
    fragColor = vec4(clamp(co, 0.0, 1.0), ao);   // co is already premultiplied by ao
}
