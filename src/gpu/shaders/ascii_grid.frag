#version 440

// Glyph-billboard fragment for the non-square ASCII pass: sample the atlas
// tile chosen by ascii_grid.vert and premultiply by the pen colour.

layout(location = 0)      in vec2 v_uv;       // 0..1 across the glyph tile
layout(location = 1) flat in vec4 v_pen;
layout(location = 2) flat in vec2 v_tileOrg;   // px

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 dims;
    vec4 p0;
    vec4 p1;
    vec4 p2;           // maskCount, atlasCols, glyphW, glyphH
    vec4 p3;           // atlasTexW, atlasTexH, spaceIndex, stipple
    vec4 coverage[32];
    vec4 sortedIdx[32];
    vec4 toneLevel[2];
    vec4 toneColor[8];
    vec4 locFieldC[2];
    vec4 locSO[2];
    vec4 maskPts[5];
    vec4 grid0;
    vec4 grid1;
    vec4 gridT;
    vec4 gridD;
};

layout(binding = 2) uniform sampler2D atlasTex;   // glyph atlas, alpha = ink

void main()
{
    vec2 texSize = p3.xy;
    vec2 glyphSz = p2.zw;
    vec2 uv = (v_tileOrg + v_uv * glyphSz) / texSize;
    float ink = texture(atlasTex, uv).a;
    float a = v_pen.a * ink;
    fragColor = vec4(v_pen.rgb * a, a);
}
