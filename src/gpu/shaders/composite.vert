#version 440

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform buf {
    mat4  mvp;            // clip-space correction (backend Y conventions)
    mat4  invPlacement;   // frame px -> layer px
    vec4  sizes;          // frameW, frameH, layerW, layerH
    ivec4 modeFlags;      // x = BlendMode, y = gpuAdjust
    vec4  adjA;           // brightness, contrast, gamma, saturation
    vec4  adjB;           // levelsBlack, levelsMid, levelsWhite, grainAmp
    vec4  adjC;           // posterize, threshold, invert, unused
};

void main()
{
    v_uv = texcoord;
    gl_Position = mvp * position;
}
