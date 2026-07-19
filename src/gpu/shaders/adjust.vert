#version 440

// Fullscreen pass of the GPU adjust chain (Phase 4) — one draw per step
// (point ops / blur H / blur V / edge / unsharp / finish). The fragment
// shader does all the work; this spans the offscreen target and hands down
// uv. The UBO block must byte-match adjust.frag's (std140).

layout(location = 0) in vec2 quadPos;    // -1..1
layout(location = 1) in vec2 quadUv;     // 0..1, v=0 at the top

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform buf {
    mat4  mvp;
    vec4  sizes;
    ivec4 opFlags;
    vec4  adjA;
    vec4  adjB;
    vec4  adjC;
};

void main()
{
    v_uv = quadUv;
    gl_Position = mvp * vec4(quadPos, 0.0, 1.0);
}
