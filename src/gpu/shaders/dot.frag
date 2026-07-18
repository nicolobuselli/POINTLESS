#version 440

// Analytic SDF rasterization of one Dot Grid dot. Same geometry as
// DotGridRenderer's painter shapes: circle (r), square (half-side r,
// rounded by cornerRadius), equilateral triangle (circumradius r, apex up),
// 5-point star (outer r, inner 0.4·r). Corner rounding for the polygon
// shapes is the SDF offset trick (vertices pulled in by cr, distance − cr)
// — visually close to the CPU's bezier-rounded corners, not bit-identical.
// Output premultiplied; the pass blends One / OneMinusSrcAlpha onto a
// transparent target.

layout(location = 0)      in vec2 v_local;
layout(location = 1) flat in vec4 v_color;
layout(location = 2) flat in vec3 v_srm;    // shapeId, r, cornerRadius

layout(location = 0) out vec4 fragColor;

// iq's exact polygon SDF over up to 10 vertices.
float sdPolygon(vec2 p, vec2 v[10], int n)
{
    float d = dot(p - v[0], p - v[0]);
    float s = 1.0;
    for (int i = 0, j = n - 1; i < n; j = i, i++) {
        vec2 e = v[j] - v[i];
        vec2 w = p - v[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 c = bvec3(p.y >= v[i].y, p.y < v[j].y, e.x * w.y > e.y * w.x);
        if (all(c) || all(not(c))) s *= -1.0;
    }
    return s * sqrt(d);
}

void main()
{
    int   shape = int(v_srm.x + 0.5);
    float r     = v_srm.y;
    float cr    = clamp(v_srm.z, 0.0, r);
    vec2  p     = v_local;

    float d;
    if (shape == 1) {                       // square, half-side r
        vec2 q = abs(p) - vec2(r - cr);
        d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - cr;
    } else if (shape == 2) {                // triangle, circumradius r, apex up
        vec2 v3[10];
        for (int i = 0; i < 3; ++i) {
            float a = -1.5707963 + float(i) * 2.0943951;
            v3[i] = vec2(cos(a), sin(a)) * max(r - cr, 0.0);
        }
        d = sdPolygon(p, v3, 3) - cr;
    } else if (shape == 3) {                // 5-point star
        vec2 v10[10];
        for (int i = 0; i < 10; ++i) {
            float a   = -1.5707963 + float(i) * 0.62831853;
            float rad = (i % 2 == 0) ? r : r * 0.4;
            v10[i] = vec2(cos(a), sin(a)) * max(rad - cr, 0.0);
        }
        d = sdPolygon(p, v10, 10) - cr;
    } else {                                // circle
        d = length(p) - r;
    }

    float aa  = max(fwidth(d), 1e-4);
    float covr = clamp(0.5 - d / aa, 0.0, 1.0);
    float a = v_color.a * covr;
    fragColor = vec4(v_color.rgb * a, a);
}
