#include "AnimParams.h"

#include <QtGlobal>
#include <array>

namespace {

// Indexed by int(ParamId); order MUST match the enum.
const std::array<ParamDesc, int(ParamId::Count)> kDescs = {{
    // label,             lo,    hi,   isInt, scope
    { "Brightness",      -100,   100,  true,  ParamScope::AllLayers },
    { "Contrast",        -100,   100,  true,  ParamScope::AllLayers },
    { "Gamma (adj)",       10,   300,  true,  ParamScope::AllLayers },
    { "Levels black",       0,   255,  true,  ParamScope::AllLayers },
    { "Levels mid",        10,   500,  true,  ParamScope::AllLayers },
    { "Levels white",       0,   255,  true,  ParamScope::AllLayers },
    { "Saturation",      -100,   100,  true,  ParamScope::AllLayers },
    { "Size %",            10,   200,  true,  ParamScope::AllLayers },
    { "Sharpen",            0,   100,  true,  ParamScope::AllLayers },
    { "Sharpen radius",     1,    10,  true,  ParamScope::AllLayers },
    { "Edge enhance",       0,   100,  true,  ParamScope::AllLayers },
    { "Blur",               0,   100,  true,  ParamScope::AllLayers },
    { "Grain",              0,   100,  true,  ParamScope::AllLayers },
    { "Posterize",          2,   256,  true,  ParamScope::AllLayers },
    { "Threshold",          0,   255,  true,  ParamScope::AllLayers },

    { "Spacing",            2,   500,  false, ParamScope::Halftone },
    { "Point spacing",      2,   200,  false, ParamScope::Halftone },
    { "Rotation",           0,   360,  false, ParamScope::Halftone },
    { "Diameter",         0.1,   3.0,  false, ParamScope::Halftone },
    { "Stretch",          0.1,   4.0,  false, ParamScope::Halftone },
    { "Stretch angle",      0,   360,  false, ParamScope::Halftone },
    { "Input DPI",         18,   300,  true,  ParamScope::Halftone },
    { "Shape threshold",    0,   255,  true,  ParamScope::Halftone },
    { "Gamma",            0.1,   5.0,  false, ParamScope::Halftone },
    { "Weight",             0,     1,  false, ParamScope::Halftone },
    { "Jitter",             0,     1,  false, ParamScope::Halftone },
    { "Opacity",            0,     1,  false, ParamScope::Halftone },
    { "Corner radius",      0,   100,  false, ParamScope::Halftone },

    { "Pixel size",         1,    16,  true,  ParamScope::Dither },
    { "Strength",           0,   100,  true,  ParamScope::Dither },
    { "Opacity",            0,     1,  false, ParamScope::Dither },
    { "Corner radius",      0,   100,  false, ParamScope::Dither },
    { "Threshold",          0,   100,  true,  ParamScope::Dither },
    { "Levels",             2,    16,  true,  ParamScope::Dither },
    { "Line angle",         0,   180,  false, ParamScope::Dither },
    { "Line spacing",       2,    32,  true,  ParamScope::Dither },

    { "Cell size",          4,    48,  true,  ParamScope::Ascii },
    { "Gamma",            0.1,   5.0,  false, ParamScope::Ascii },
    { "Edges",              0,   100,  true,  ParamScope::Ascii },

    { "Threshold 1",        0,   255,  true,  ParamScope::Tonal },
    { "Threshold 2",        0,   255,  true,  ParamScope::Tonal },
    { "Threshold 3",        0,   255,  true,  ParamScope::Tonal },
    { "Threshold 4",        0,   255,  true,  ParamScope::Tonal },
    { "Threshold 5",        0,   255,  true,  ParamScope::Tonal },
    { "Threshold 6",        0,   255,  true,  ParamScope::Tonal },
    { "Threshold 7",        0,   255,  true,  ParamScope::Tonal },
    { "Threshold 8",        0,   255,  true,  ParamScope::Tonal },

    { "Background opacity", 0,     1,  false, ParamScope::Document },
}};

// The tonal palette of the effect matching the layer's kind (nullptr for
// Original, which has no fill).
const TonalSettings* layerTonal(const Layer& l)
{
    switch (l.kind) {
        case LayerKind::Halftone: return &l.halftone.tonal;
        case LayerKind::Dither:   return &l.dither.tonal;
        case LayerKind::Ascii:    return &l.ascii.tonal;
        default:                  return nullptr;
    }
}
TonalSettings* layerTonal(Layer& l)
{
    return const_cast<TonalSettings*>(layerTonal(const_cast<const Layer&>(l)));
}

// 0-based tone index for a ToneLevelN id, or -1 if id isn't a tone level.
int toneLevelIndex(ParamId id)
{
    if (int(id) >= int(ParamId::ToneLevel1) && int(id) <= int(ParamId::ToneLevel8))
        return int(id) - int(ParamId::ToneLevel1);
    return -1;
}

} // namespace

const ParamDesc& paramDesc(ParamId id)
{
    return kDescs[size_t(id)];
}

double getParam(const Layer& l, ParamId id)
{
    if (const int ti = toneLevelIndex(id); ti >= 0) {
        const TonalSettings* t = layerTonal(l);
        return (t && ti < int(t->tones.size())) ? double(t->tones[size_t(ti)].level) : 0.0;
    }
    switch (id) {
        case ParamId::AdjBrightness:      return l.adjustments.brightness;
        case ParamId::AdjContrast:        return l.adjustments.contrast;
        case ParamId::AdjGamma:           return l.adjustments.gamma;
        case ParamId::AdjLevelsBlack:     return l.adjustments.levelsBlack;
        case ParamId::AdjLevelsMid:       return l.adjustments.levelsMid;
        case ParamId::AdjLevelsWhite:     return l.adjustments.levelsWhite;
        case ParamId::AdjSaturation:      return l.adjustments.saturation;
        case ParamId::AdjSizePct:         return l.adjustments.sizePct;
        case ParamId::AdjSharpenStrength: return l.adjustments.sharpenStrength;
        case ParamId::AdjSharpenRadius:   return l.adjustments.sharpenRadius;
        case ParamId::AdjEdgeEnhancement: return l.adjustments.edgeEnhancement;
        case ParamId::AdjBlur:            return l.adjustments.blur;
        case ParamId::AdjGrain:           return l.adjustments.grain;
        case ParamId::AdjPosterize:       return l.adjustments.posterize;
        case ParamId::AdjThreshold:       return l.adjustments.threshold;

        case ParamId::HtGridSpacing:       return l.halftone.grid.spacing;
        case ParamId::HtGridPointSpacing:  return l.halftone.grid.pointSpacing;
        case ParamId::HtGridRotation:      return l.halftone.grid.rotation;
        case ParamId::HtGridDiameter:      return l.halftone.grid.diameter;
        case ParamId::HtGridStretchFactor: return l.halftone.grid.stretchFactor;
        case ParamId::HtGridStretchAngle:  return l.halftone.grid.stretchAngle;
        case ParamId::HtInputDpi:          return l.halftone.inputDpi;
        case ParamId::HtMultiThreshold:    return l.halftone.multiThreshold;
        case ParamId::HtGamma:             return l.halftone.gamma;
        case ParamId::HtWeight:            return l.halftone.weight;
        case ParamId::HtJitter:            return l.halftone.jitter;
        case ParamId::HtOpacity:           return l.halftone.opacity;
        case ParamId::HtCornerRadius:      return l.halftone.cornerRadius;

        case ParamId::DiPixelSize:    return l.dither.pixelSize;
        case ParamId::DiStrength:     return l.dither.strength;
        case ParamId::DiOpacity:      return l.dither.opacity;
        case ParamId::DiCornerRadius: return l.dither.cornerRadius;
        case ParamId::DiThreshold:    return l.dither.threshold;
        case ParamId::DiLevels:       return l.dither.levels;
        case ParamId::DiLineAngle:    return l.dither.lineAngle;
        case ParamId::DiLineSpacing:  return l.dither.lineSpacing;

        case ParamId::AsCellSize: return l.ascii.cellSize;
        case ParamId::AsGamma:    return l.ascii.gamma;
        case ParamId::AsEdges:    return l.ascii.edges;

        default: return 0.0;   // Document params handled elsewhere
    }
}

void setParam(Layer& l, ParamId id, double v)
{
    const ParamDesc& d = paramDesc(id);
    v = qBound(d.lo, v, d.hi);
    const int   iv = qRound(v);
    const float fv = float(v);

    if (const int ti = toneLevelIndex(id); ti >= 0) {
        TonalSettings* t = layerTonal(l);
        if (t && ti < int(t->tones.size())) t->tones[size_t(ti)].level = iv;
        return;
    }

    switch (id) {
        case ParamId::AdjBrightness:      l.adjustments.brightness      = iv; break;
        case ParamId::AdjContrast:        l.adjustments.contrast        = iv; break;
        case ParamId::AdjGamma:           l.adjustments.gamma           = iv; break;
        case ParamId::AdjLevelsBlack:     l.adjustments.levelsBlack     = iv; break;
        case ParamId::AdjLevelsMid:       l.adjustments.levelsMid       = iv; break;
        case ParamId::AdjLevelsWhite:     l.adjustments.levelsWhite     = iv; break;
        case ParamId::AdjSaturation:      l.adjustments.saturation      = iv; break;
        case ParamId::AdjSizePct:         l.adjustments.sizePct         = iv; break;
        case ParamId::AdjSharpenStrength: l.adjustments.sharpenStrength = iv; break;
        case ParamId::AdjSharpenRadius:   l.adjustments.sharpenRadius   = iv; break;
        case ParamId::AdjEdgeEnhancement: l.adjustments.edgeEnhancement = iv; break;
        case ParamId::AdjBlur:            l.adjustments.blur            = iv; break;
        case ParamId::AdjGrain:           l.adjustments.grain           = iv; break;
        case ParamId::AdjPosterize:       l.adjustments.posterize       = iv; break;
        case ParamId::AdjThreshold:       l.adjustments.threshold       = iv; break;

        case ParamId::HtGridSpacing:       l.halftone.grid.spacing       = fv; break;
        case ParamId::HtGridPointSpacing:  l.halftone.grid.pointSpacing  = fv; break;
        case ParamId::HtGridRotation:      l.halftone.grid.rotation      = fv; break;
        case ParamId::HtGridDiameter:      l.halftone.grid.diameter      = fv; break;
        case ParamId::HtGridStretchFactor: l.halftone.grid.stretchFactor = fv; break;
        case ParamId::HtGridStretchAngle:  l.halftone.grid.stretchAngle  = fv; break;
        case ParamId::HtInputDpi:          l.halftone.inputDpi           = iv; break;
        case ParamId::HtMultiThreshold:    l.halftone.multiThreshold     = iv; break;
        case ParamId::HtGamma:             l.halftone.gamma              = fv; break;
        case ParamId::HtWeight:            l.halftone.weight             = fv; break;
        case ParamId::HtJitter:            l.halftone.jitter             = fv; break;
        case ParamId::HtOpacity:           l.halftone.opacity            = fv; break;
        case ParamId::HtCornerRadius:      l.halftone.cornerRadius       = fv; break;

        case ParamId::DiPixelSize:    l.dither.pixelSize    = iv; break;
        case ParamId::DiStrength:     l.dither.strength     = iv; break;
        case ParamId::DiOpacity:      l.dither.opacity      = fv; break;
        case ParamId::DiCornerRadius: l.dither.cornerRadius = fv; break;
        case ParamId::DiThreshold:    l.dither.threshold    = iv; break;
        case ParamId::DiLevels:       l.dither.levels       = iv; break;
        case ParamId::DiLineAngle:    l.dither.lineAngle    = fv; break;
        case ParamId::DiLineSpacing:  l.dither.lineSpacing  = iv; break;

        case ParamId::AsCellSize: l.ascii.cellSize = iv; break;
        case ParamId::AsGamma:    l.ascii.gamma    = fv; break;
        case ParamId::AsEdges:    l.ascii.edges    = iv; break;

        default: break;   // Document params handled elsewhere
    }
}

double getDocParam(const SessionParams& p, ParamId id)
{
    switch (id) {
        case ParamId::BackgroundOpacity: return p.backgroundOpacity;
        default: return 0.0;
    }
}

void setDocParam(SessionParams& p, ParamId id, double v)
{
    const ParamDesc& d = paramDesc(id);
    v = qBound(d.lo, v, d.hi);
    switch (id) {
        case ParamId::BackgroundOpacity: p.backgroundOpacity = float(v); break;
        default: break;
    }
}

std::vector<ParamId> animatableParams(const Layer& layer)
{
    ParamScope kindScope = ParamScope::Halftone;
    switch (layer.kind) {
        case LayerKind::Halftone: kindScope = ParamScope::Halftone; break;
        case LayerKind::Dither:   kindScope = ParamScope::Dither;   break;
        case LayerKind::Ascii:    kindScope = ParamScope::Ascii;    break;
        case LayerKind::Original: kindScope = ParamScope::AllLayers; break;  // adjustments only
    }

    std::vector<ParamId> out;
    for (int i = 0; i < int(ParamId::Count); ++i) {
        const ParamId id = ParamId(i);
        const ParamScope s = paramDesc(id).scope;
        if (s == ParamScope::AllLayers || s == kindScope)
            out.push_back(id);
    }

    // Per-colour thresholds: only meaningful with more than one tone, and only
    // for as many tones as the active palette currently has.
    if (const TonalSettings* t = layerTonal(layer); t && t->tones.size() > 1) {
        const int n = qMin(int(t->tones.size()), kMaxToneLevels);
        for (int i = 0; i < n; ++i)
            out.push_back(toneLevelParam(i));
    }
    return out;
}
