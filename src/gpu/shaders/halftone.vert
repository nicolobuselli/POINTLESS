#version 440

// Fullscreen content pass for the Halftone (AM screen) layer — the fragment
// shader does all the work; this just spans the offscreen target and hands
// down uv. The UBO block must byte-match halftone.frag's (std140).

layout(location = 0) in vec2 quadPos;    // -1..1
layout(location = 1) in vec2 quadUv;     // 0..1, v=0 at the top

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 dims;
    vec4 pA;
    vec4 pB;
    vec4 pC;
    vec4 paper;
    vec4 scrA[8];
    vec4 scrB[8];
    vec4 inks[8];
};

void main()
{
    v_uv = quadUv;
    gl_Position = mvp * vec4(quadPos, 0.0, 1.0);
}
