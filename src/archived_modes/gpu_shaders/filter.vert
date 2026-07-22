#version 440

// Fullscreen content pass shared by the whole-image GPU filters (Thermal,
// Blueprint, Metal Emboss, Glitch/Datamosh, Glitch/CRT) — one pipeline, one
// fragment shader with an op switch (see filter.frag), several draws per
// layer per frame (blur/sobel/dilate pre-passes + the final fill). The UBO
// block must byte-match filter.frag's (std140).

layout(location = 0) in vec2 quadPos;    // -1..1
layout(location = 1) in vec2 quadUv;     // 0..1, v=0 at the top

layout(location = 0) out vec2 v_uv;

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

void main()
{
    v_uv = quadUv;
    gl_Position = mvp * vec4(quadPos, 0.0, 1.0);
}
