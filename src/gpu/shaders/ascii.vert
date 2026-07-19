#version 440

// Fullscreen content pass for GPU ASCII (coverage-ramp path) — the fragment
// shader does all the work; this spans the offscreen target and hands down
// uv. The UBO block must byte-match ascii.frag's (std140).

layout(location = 0) in vec2 quadPos;    // -1..1
layout(location = 1) in vec2 quadUv;     // 0..1, v=0 at the top

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 dims;
    vec4 grid;
    vec4 atlas;
    vec4 pA;
    vec4 pB;
    vec4 pC;
    vec4 coverage[32];
    vec4 sortedIdx[32];
    vec4 toneLevel[2];
    vec4 toneColor[8];
    vec4 locFieldC[5];
    vec4 locSO[5];
    vec4 maskPts[5];
};

void main()
{
    v_uv = quadUv;
    gl_Position = mvp * vec4(quadPos, 0.0, 1.0);
}
