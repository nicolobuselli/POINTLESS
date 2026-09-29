#version 440

// Mosaic tile fragment — analytic rounded-rect coverage with fwidth AA.
// v_local is the unrotated tile-space offset from the centre.

layout(location = 0)      in vec2 v_local;
layout(location = 1) flat in vec4 v_color;   // premultiplied
layout(location = 2) flat in vec3 v_whr;     // tw, th, corner radius px
layout(location = 3) flat in vec4 v_textUv;
layout(location = 4) flat in vec2 v_textSize;
layout(location = 5) flat in vec4 v_textColor;

layout(location = 0) out vec4 fragColor;

layout(binding = 2) uniform sampler2D textAtlas;

void main()
{
    vec2  half_ = v_whr.xy * 0.5;
    float rad   = min(v_whr.z, min(half_.x, half_.y));
    vec2  q     = abs(v_local) - (half_ - rad);
    float d     = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - rad;
    float aa    = max(fwidth(d), 1e-4);
    float cov   = clamp(0.5 - d / aa, 0.0, 1.0);
    vec4 fill = v_color * cov;
    vec4 text = vec4(0.0);
    if (v_textSize.x > 0.0 && v_textSize.y > 0.0) {
        vec2 tuv = v_local / v_textSize + vec2(0.5);
        if (all(greaterThanEqual(tuv, vec2(0.0))) && all(lessThanEqual(tuv, vec2(1.0)))) {
            float a = texture(textAtlas, v_textUv.xy + tuv * v_textUv.zw).a * v_textColor.a;
            text = vec4(v_textColor.rgb * a, a);
        }
    }
    fragColor = text + fill * (1.0 - text.a);
}
