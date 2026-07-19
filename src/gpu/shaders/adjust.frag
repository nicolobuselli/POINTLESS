#version 440

// GPU adjust chain (Phase 4) — faithful port of ImageAdjuster.cpp, one op
// per fullscreen pass, ping-ponging RGBA16F targets (KEEP IN SYNC with
// ImageAdjuster.cpp AND with planAdjustPasses/fillAdjustUbo in
// GpuCanvasWidget.cpp). All math in straight alpha, sRGB-domain floats
// (CPU works in bytes: ±1 lsb quantization divergence accepted).
//
// Ops (opFlags.x):
//   0 point1  — resize (implicit: sample src at out res, mipmapped) +
//               brightness/contrast/gamma/levels/saturation/invert
//   1 blurH   — box blur, horizontal, radius opFlags.y (alpha untouched)
//   2 blurV   — box blur, vertical
//   3 edge    — Laplacian edge enhance, strength adjC.w (borders = 0)
//   4 unsharp — texA = H-blurred copy (V-blurred here), texB = original:
//               out = orig + (orig - blurred) * adjC.w
//   5 finish  — grain/posterize/threshold, then per opFlags.z:
//               bit0: flatten onto white + sRGB→linear, alpha=1 (screen srcs)
//               bit1: premultiply (composite raster path)

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4  mvp;
    vec4  sizes;      // outW, outH, texAW, texAH
    ivec4 opFlags;    // x = op, y = blur radius, z = finish flags
    vec4  adjA;       // brightness -100..100, contrast -100..100, gamma (actual), saturation 0..200
    vec4  adjB;       // levelsBlack 0..255, levelsMid (actual), levelsWhite 0..255, grain amp 0..1
    vec4  adjC;       // posterize (256 = off), threshold (0 = off), invert 0/1, amount (edge/unsharp)
};

layout(binding = 1) uniform sampler2D texA;   // primary input of this pass
layout(binding = 2) uniform sampler2D texB;   // op 4 only: pre-blur original

// Integer-LUT luma weights of ImageAdjuster ((r*54 + g*183 + b*19) >> 8).
float lumA(vec3 c) { return dot(c, vec3(54.0, 183.0, 19.0) / 256.0); }

// ImageAdjuster's hash2d (grain).
uint hashAdj(uvec2 p)
{
    uint h = p.x * 73856093u ^ p.y * 19349663u;
    h ^= (h >> 16);
    h *= 0x45d9f3bu;
    h ^= (h >> 16);
    return h;
}

// IEC sRGB → linear (the float form of ColorMath's LUT).
vec3 srgbToLinear(vec3 c)
{
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + vec3(0.055)) / 1.055, vec3(2.4));
    return mix(hi, lo, lessThanEqual(c, vec3(0.04045)));
}

vec3 pointOps(vec3 c)
{
    // Brightness / contrast (0..255-domain formula, normalized)
    if (adjA.x != 0.0 || adjA.y != 0.0) {
        float ct = adjA.y * 2.55;
        float factor = (259.0 * (ct + 255.0)) / (255.0 * (259.0 - ct));
        float off = adjA.x * 1.275 / 255.0;
        c = clamp(factor * (c - 128.0 / 255.0) + 128.0 / 255.0 + off, 0.0, 1.0);
    }
    if (adjA.z != 1.0)
        c = pow(c, vec3(adjA.z));
    if (adjB.x != 0.0 || adjB.y != 1.0 || adjB.z != 255.0) {
        float range = adjB.z - adjB.x;
        vec3 v = (range > 0.0) ? (c * 255.0 - adjB.x) / range : vec3(0.0);
        v = clamp(v, 0.0, 1.0);
        if (adjB.y != 1.0) v = pow(v, vec3(1.0 / adjB.y));
        c = v;
    }
    if (adjA.w != 100.0) {
        float gray = lumA(c);
        c = clamp(gray + (c - gray) * adjA.w / 100.0, 0.0, 1.0);
    }
    if (adjC.z > 0.5)
        c = 1.0 - c;
    return c;
}

void main()
{
    int op = opFlags.x;

    if (op == 0) {
        vec4 s = texture(texA, v_uv);   // mip sampler: implicit sizePct resize
        fragColor = vec4(pointOps(s.rgb), s.a);
        return;
    }

    if (op == 1 || op == 2) {
        vec2 texel = 1.0 / sizes.zw;
        vec2 dir = (op == 1) ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
        int r = opFlags.y;
        vec3 sum = vec3(0.0);
        for (int i = -r; i <= r; ++i)
            sum += texture(texA, v_uv + float(i) * dir).rgb;   // ClampToEdge = CPU qBound
        fragColor = vec4(sum / float(2 * r + 1), texture(texA, v_uv).a);
        return;
    }

    if (op == 3) {
        vec4 s = texture(texA, v_uv);
        vec2 px = floor(v_uv * sizes.xy);
        float e = 0.0;
        // CPU leaves the 1px border at lap = 0.
        if (px.x >= 1.0 && px.x <= sizes.x - 2.0 && px.y >= 1.0 && px.y <= sizes.y - 2.0) {
            vec2 t = 1.0 / sizes.zw;
            float lc = lumA(s.rgb);
            float lap = 4.0 * lc
                      - lumA(texture(texA, v_uv - vec2(t.x, 0.0)).rgb)
                      - lumA(texture(texA, v_uv + vec2(t.x, 0.0)).rgb)
                      - lumA(texture(texA, v_uv - vec2(0.0, t.y)).rgb)
                      - lumA(texture(texA, v_uv + vec2(0.0, t.y)).rgb);
            e = lap * adjC.w;
        }
        fragColor = vec4(clamp(s.rgb + e, 0.0, 1.0), s.a);
        return;
    }

    if (op == 4) {
        vec2 texel = 1.0 / sizes.zw;
        int r = opFlags.y;
        vec3 sum = vec3(0.0);
        for (int i = -r; i <= r; ++i)
            sum += texture(texA, v_uv + vec2(0.0, float(i) * texel.y)).rgb;
        vec3 blurred = sum / float(2 * r + 1);
        vec4 orig = texture(texB, v_uv);
        fragColor = vec4(clamp(orig.rgb + (orig.rgb - blurred) * adjC.w, 0.0, 1.0), orig.a);
        return;
    }

    // op == 5: finish
    vec4 s = texture(texA, v_uv);
    vec3 c = s.rgb;
    float a = s.a;
    if (adjB.w > 0.0) {
        uint h = hashAdj(uvec2(max(vec2(0.0), floor(v_uv * sizes.xy))));
        float n = (float(h & 0xFFFFu) / 65535.0 * 2.0 - 1.0) * adjB.w;
        c = clamp(c + n, 0.0, 1.0);
    }
    if (adjC.x < 256.0) {
        float lv = max(1.0, adjC.x - 1.0);
        c = clamp(round(c * lv) / lv, 0.0, 1.0);
    }
    if (adjC.y > 0.0) {
        float l = lumA(c) * 255.0;
        c = vec3((l >= adjC.y) ? 1.0 : 0.0);
    }
    if ((opFlags.z & 1) != 0) {          // screen source: flatten + linearize
        c = mix(vec3(1.0), c, a);
        c = srgbToLinear(c);
        a = 1.0;
    }
    if ((opFlags.z & 2) != 0)            // composite raster: premultiply
        c *= a;
    fragColor = vec4(c, a);
}
