#pragma once

#include "Params.h"
#include <vector>

// ============================================================
//  AnimParams — addressing layer for the animation system.
//
//  Every UI control is hand-wired to a struct field, so there is no
//  generic "parameter path". This maps a stable ParamId to the
//  matching numeric field of a Layer (or the document), giving the
//  keyframe system a uniform get/set/enumerate interface over all
//  animatable scalar parameters.
//
//  Values are the RAW stored field values (the UI does its own ×/÷100
//  display conversions); ranges below are in those raw units.
// ============================================================

enum class ParamScope { AllLayers, Halftone, Dither, Ascii, Tonal, Document };

// Max animatable per-colour thresholds (matches the tonal UI's kMaxTones).
constexpr int kMaxToneLevels = 8;

enum class ParamId {
    // Adjustments (every layer)
    AdjBrightness, AdjContrast, AdjGamma, AdjLevelsBlack, AdjLevelsMid,
    AdjLevelsWhite, AdjSaturation, AdjSizePct, AdjSharpenStrength, AdjSharpenRadius,
    AdjEdgeEnhancement, AdjBlur, AdjGrain, AdjPosterize, AdjThreshold,
    // Halftone (grid + own)
    HtGridSpacing, HtGridPointSpacing, HtGridRotation, HtGridDiameter,
    HtGridStretchFactor, HtGridStretchAngle, HtInputDpi, HtMultiThreshold,
    HtGamma, HtJitter, HtOpacity, HtCornerRadius,
    // Dither
    DiPixelSize, DiStrength, DiOpacity, DiCornerRadius, DiThreshold,
    // Ascii
    AsCellSize, AsGamma,
    // Tonal — per-colour threshold of the active effect's palette (only the
    // first N are animatable, N = current tone count). MUST stay contiguous.
    ToneLevel1, ToneLevel2, ToneLevel3, ToneLevel4,
    ToneLevel5, ToneLevel6, ToneLevel7, ToneLevel8,
    // Document
    BackgroundOpacity,

    Count
};

inline ParamId toneLevelParam(int index) {
    return ParamId(int(ParamId::ToneLevel1) + index);
}

struct ParamDesc {
    const char* label;
    double      lo;
    double      hi;
    bool        isInt;
    ParamScope  scope;
};

// Descriptor (label, range, int-ness, scope) for a parameter.
const ParamDesc& paramDesc(ParamId id);

inline bool isDocumentParam(ParamId id) { return paramDesc(id).scope == ParamScope::Document; }

// Layer-scoped get/set (do NOT call with a Document param).
double getParam(const Layer& layer, ParamId id);
void   setParam(Layer& layer, ParamId id, double value);

// Document-scoped get/set (background, etc.).
double getDocParam(const SessionParams& p, ParamId id);
void   setDocParam(SessionParams& p, ParamId id, double value);

// Parameters that make sense to animate for this layer:
// the shared Adjustments plus the ones matching the layer's kind.
std::vector<ParamId> animatableParams(const Layer& layer);
