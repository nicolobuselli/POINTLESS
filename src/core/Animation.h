#pragma once

#include "AnimParams.h"
#include "Params.h"
#include <vector>
#include <QSet>

// ============================================================
//  Animation — keyframe model over the addressable parameters.
//
//  A Track animates one (layerId, ParamId) with a sorted list of
//  keyframes; each keyframe's easing governs the segment to its
//  right. paramsAtFrame() bakes all tracks onto a base SessionParams
//  to produce the parameters for a given timeline frame.
// ============================================================

enum class Easing { Linear, Hold, EaseIn, EaseOut, EaseInOut };

struct Keyframe {
    int    frame  = 0;
    double value  = 0.0;
    Easing easing = Easing::Linear;   // interpolation from this key to the next
};
inline bool operator==(const Keyframe& a, const Keyframe& b) {
    return a.frame == b.frame && a.value == b.value && a.easing == b.easing;
}

struct Track {
    int                   layerId = -1;                  // -1 = document (background)
    ParamId               param   = ParamId::AdjBrightness;
    std::vector<Keyframe> keys;                          // sorted by frame
};
inline bool operator==(const Track& a, const Track& b) {
    return a.layerId == b.layerId && a.param == b.param && a.keys == b.keys;
}

struct Animation {
    std::vector<Track> tracks;
    int frameStart = 0;
    int frameEnd   = 120;
    int fps        = 24;
    int playhead   = 0;

    bool hasAnimation() const { return !tracks.empty(); }
};
// playhead is a view position, not document content → excluded from equality.
inline bool operator==(const Animation& a, const Animation& b) {
    return a.tracks == b.tracks && a.frameStart == b.frameStart
        && a.frameEnd == b.frameEnd && a.fps == b.fps;
}
inline bool operator!=(const Animation& a, const Animation& b) { return !(a == b); }

// Raw interpolated value of a track at a frame.
double evaluate(const Track& track, int frame);

// Bake every track at `frame` onto a copy of `base`.
SessionParams paramsAtFrame(const SessionParams& base, const Animation& anim, int frame);

// Track lookup + keyframe upsert (used by auto-key and the timeline).
Track* findTrack(Animation& anim, int layerId, ParamId param);
void   upsertKey(Animation& anim, int layerId, ParamId param, int frame,
                 double value, Easing easing = Easing::Linear);

// Drop every track belonging to a deleted layer, so its keyframes don't
// linger as orphaned/invisible tracks in the timeline.
void removeLayerTracks(Animation& anim, int layerId);

// Which params currently have a keyframe track for this layer (-1 = document),
// for UI "this parameter is animated" indicators.
QSet<ParamId> animatedParamIds(const Animation& anim, int layerId);
