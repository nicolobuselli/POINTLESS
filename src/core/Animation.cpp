#include "Animation.h"

#include <cmath>

namespace {

// Map normalised t (0..1) through the easing of the segment's left key.
double easeT(Easing e, double t)
{
    switch (e) {
        case Easing::Linear:   return t;
        case Easing::Hold:     return 0.0;                 // step: hold left value
        case Easing::EaseIn:   return t * t * t;           // cubic in
        case Easing::EaseOut:  { const double u = 1.0 - t; return 1.0 - u * u * u; }
        case Easing::EaseInOut:
            return t < 0.5 ? 4.0 * t * t * t
                           : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    }
    return t;
}

} // namespace

double evaluate(const Track& tr, int frame)
{
    const auto& k = tr.keys;
    if (k.empty())                return 0.0;
    if (frame <= k.front().frame) return k.front().value;
    if (frame >= k.back().frame)  return k.back().value;

    int i = 0;
    while (i + 1 < int(k.size()) && k[size_t(i) + 1].frame <= frame) ++i;
    const Keyframe& a = k[size_t(i)];
    const Keyframe& b = k[size_t(i) + 1];
    if (b.frame == a.frame) return b.value;

    double t = double(frame - a.frame) / double(b.frame - a.frame);
    t = easeT(a.easing, t);
    return a.value + (b.value - a.value) * t;
}

SessionParams paramsAtFrame(const SessionParams& base, const Animation& anim, int frame)
{
    SessionParams p = base;
    for (const Track& tr : anim.tracks) {
        if (tr.keys.empty()) continue;
        const double v = evaluate(tr, frame);
        if (tr.layerId < 0 || isDocumentParam(tr.param)) {
            setDocParam(p, tr.param, v);
        } else {
            const int idx = findLayerById(p.layers, tr.layerId);
            if (idx >= 0) setParam(p.layers[size_t(idx)], tr.param, v);
        }
    }
    return p;
}

Track* findTrack(Animation& anim, int layerId, ParamId param)
{
    for (Track& t : anim.tracks)
        if (t.layerId == layerId && t.param == param) return &t;
    return nullptr;
}

void upsertKey(Animation& anim, int layerId, ParamId param, int frame,
               double value, Easing easing)
{
    Track* t = findTrack(anim, layerId, param);
    if (!t) {
        anim.tracks.push_back(Track{ layerId, param, {} });
        t = &anim.tracks.back();
    }

    for (Keyframe& kf : t->keys) {
        if (kf.frame == frame) { kf.value = value; return; }   // keep existing easing
    }

    Keyframe nk{ frame, value, easing };
    auto it = t->keys.begin();
    while (it != t->keys.end() && it->frame < frame) ++it;
    t->keys.insert(it, nk);
}
