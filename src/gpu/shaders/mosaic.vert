#version 440

// Instanced Mosaic tile-fill content pass — fully uniform-driven, a port of
// MosaicRenderer::render's fill path (KEEP IN SYNC, incl. fillMosaicParams
// in GpuCanvasWidget.cpp). The lattice reconstruction from gl_InstanceIndex
// is the same GridGenerator port as dot.vert (KEEP IN SYNC with both).
// Tile colour = mip average of the linear source under the cell window
// (clamped to the frame like the CPU's sampleRect); tiles rotate with the
// lattice about their own centre. Palette fill uses OkLab; text is CPU-only.

layout(location = 0) in vec2 quadPos;    // fullscreen-quad vertex, -1..1
layout(location = 1) in vec2 quadUv;     // unused

layout(location = 0)      out vec2 v_local;   // px offset from tile centre (unrotated)
layout(location = 1) flat out vec4 v_color;   // premultiplied fill
layout(location = 2) flat out vec3 v_whr;     // tw, th, corner radius px
layout(location = 3) flat out vec4 v_textUv;  // atlas UV origin + size
layout(location = 4) flat out vec2 v_textSize;// fitted text size in tile px
layout(location = 5) flat out vec4 v_textColor; // straight text colour

layout(std140, binding = 0) uniform buf {
    mat4 mvp;          // content px -> clip
    vec4 dims;         // contentW, contentH, spacing, lod
    vec4 p0;           // cellPxW, cellPxH, gridRotation rad, opacity
    vec4 p1;           // cornerRadius, colorMode (1 image, 0 fixed, -1 palette), nTones, paperCut
    vec4 p2;           // maskCount, 0, 0, 0
    vec4 toneLevel[2]; // up to 8 tone levels, 0..255
    vec4 toneColor[8]; // sRGB rgb + per-tone opacity
    vec4 locFieldC[3]; // Spc, Wid, Hei: cx, cy, rIn, rOut (content px)
    vec4 locScale;     // per-field multiplier (x,y,z)
    vec4 locOn;        // per-field enable
    vec4 maskPts[5];   // enabled loc circles
    vec4 grid0;        // gridType, i0, j0, cols   (GridGpuLayout)
    vec4 grid1;        // ringN, margin, 0, 0
    vec4 gridT;        // m11, m12, m21, m22
    vec4 gridD;        // dx, dy, cx, cy
    vec4 textRect[8];  // normalized atlas UV rect per tone
    vec4 textColor[8]; // explicit RGBA; alpha < 0 = auto contrast
    vec4 textMeta;     // atlasW, atlasH, base padding fraction, nonempty count
    vec4 locPad;       // text-padding localization circle
    vec4 locPadSO;     // scale, on, 0, 0
};

layout(binding = 1) uniform sampler2D srcTex;   // linear light, mipmapped

const float TAU = 6.28318530718;

float lin2s(float v)
{
    v = clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

float srgbLinear(float v)
{
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

vec3 paletteLab(vec3 srgb)
{
    vec3 c = vec3(srgbLinear(srgb.r), srgbLinear(srgb.g), srgbLinear(srgb.b));
    vec3 lms = pow(max(vec3(
        dot(c, vec3(0.4122214708, 0.5363325363, 0.0514459929)),
        dot(c, vec3(0.2119034982, 0.6806995451, 0.1073969566)),
        dot(c, vec3(0.0883024619, 0.2817188376, 0.6299787005))), vec3(0.0)), vec3(1.0 / 3.0));
    return vec3(dot(lms, vec3(0.2104542553, 0.7936177850, -0.0040720468)),
                dot(lms, vec3(1.9779984951, -2.4285922050, 0.4505937099)),
                dot(lms, vec3(0.0259040371, 0.7827717662, -0.8086757660)));
}

int paletteIndex(vec3 linearColor, int count)
{
    vec3 srgb = round(vec3(lin2s(linearColor.r), lin2s(linearColor.g),
                           lin2s(linearColor.b)) * 255.0) / 255.0;
    vec3 target = paletteLab(srgb);
    int best = 0;
    float bestDistance = 1e30;
    for (int i = 0; i < count; ++i) {
        vec3 delta = target - paletteLab(toneColor[i].rgb);
        float d = dot(delta, delta);
        if (d < bestDistance) { bestDistance = d; best = i; }
    }
    return best;
}

// LocField::t / mul / LocMask::mask — same ports as dot.vert.
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
    return locOn[i] > 0.5 ? 1.0 + (locScale[i] - 1.0) * locT(locFieldC[i], p) : 1.0;
}
float maskV(vec2 p)
{
    int n = int(p2.x + 0.5);
    if (n < 1) return 1.0;
    float best = 0.0;
    for (int i = 0; i < n; ++i) best = max(best, locT(maskPts[i], p));
    return best;
}

void cull()
{
    gl_Position = vec4(-3.0, -3.0, 0.0, 1.0);
    v_local = vec2(0.0); v_color = vec4(0.0); v_whr = vec3(0.0);
    v_textUv = vec4(0.0); v_textSize = vec2(0.0); v_textColor = vec4(0.0);
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

    // ── cell window, clamped sampling (CPU sampleRect) ─────────────────────
    float pw = p0.x, ph = p0.y;
    vec2 c0 = round(pos) - floor(vec2(pw, ph) * 0.5);   // int cellRect origin
    vec2 lo = max(c0, vec2(0.0));
    vec2 hi = min(c0 + vec2(pw, ph), dims.xy);
    if (hi.x <= lo.x || hi.y <= lo.y) { cull(); return; }   // fully off-frame

    float mv = maskV(pos);
    if (p2.x > 0.5 && mv <= 0.0) { cull(); return; }        // spotlight

    vec3  lin     = textureLod(srcTex, (lo + hi) * 0.5 / dims.xy, dims.w).rgb;
    float lumPerc = lin2s(dot(lin, vec3(0.2126, 0.7152, 0.0722)));
    if (lumPerc > p1.w) { cull(); return; }                 // ink-or-paper: paper

    // ── fill colour (pickToneIndex mirror) ─────────────────────────────────
    vec4 col;
    int toneIdx = -1;
    int nTones = int(p1.z + 0.5);
    if (p1.y > 0.5 || nTones < 1) {
        col = vec4(lin2s(lin.r), lin2s(lin.g), lin2s(lin.b), 1.0);
        if (nTones > 0) {
            int lum = int(round(lumPerc * 255.0));
            float bestDist = 512.0;
            for (int i = 0; i < nTones; ++i) {
                float d = abs(float(lum) - toneLevel[i >> 2][i & 3]);
                if (d < bestDist) { bestDist = d; toneIdx = i; }
            }
        }
    } else if (p1.y < -0.5) {
        toneIdx = paletteIndex(lin, nTones);
        col = toneColor[toneIdx];
    } else {
        int lum = int(round(lumPerc * 255.0));
        int best = 0;
        float bestDist = 512.0;
        for (int i = 0; i < nTones; ++i) {
            float d = abs(float(lum) - toneLevel[i >> 2][i & 3]);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        toneIdx = best;
        col = toneColor[best];
    }
    col.a *= p0.w * mv;

    // ── tile geometry: loc-scaled size, centred in the slot ────────────────
    float tw = pw * locMul(0, pos) * locMul(1, pos);
    float th = ph * locMul(0, pos) * locMul(2, pos);
    vec2 center = c0 + vec2(pw, ph) * 0.5;

    float rad = p1.x * min(tw, th) * 0.5;
    float pad = 1.5;                              // AA ramp margin
    vec2 local = quadPos * (vec2(tw, th) * 0.5 + pad);
    float c = cos(p0.z), s = sin(p0.z);           // tile turns with the lattice
    vec2 world = center + vec2(c * local.x - s * local.y,
                               s * local.x + c * local.y);

    v_local = local;
    v_color = vec4(col.rgb * col.a, col.a);       // premultiplied
    v_whr   = vec3(tw, th, rad);
    v_textUv = toneIdx >= 0 ? textRect[toneIdx] : vec4(0.0);
    v_textSize = vec2(0.0);
    v_textColor = vec4(0.0);
    if (toneIdx >= 0 && v_textUv.z > 0.0 && v_textUv.w > 0.0) {
        float padMul = locPadSO.y > 0.5
            ? 1.0 + (locPadSO.x - 1.0) * locT(locPad, pos) : 1.0;
        float pad = textMeta.z * padMul * min(tw, th);
        vec2 available = max(vec2(1.0), vec2(tw, th) - vec2(2.0 * pad));
        vec2 glyphPx = v_textUv.zw * textMeta.xy;
        float fit = min(available.x / glyphPx.x, available.y / glyphPx.y);
        v_textSize = glyphPx * fit;
        vec4 tc = textColor[toneIdx];
        if (tc.a < 0.0)
            tc = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722)) > 0.5
               ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(1.0);
        tc.a *= p0.w * mv;
        v_textColor = tc;
    }
    gl_Position = mvp * vec4(world, 0.0, 1.0);
}
