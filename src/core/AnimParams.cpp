#include "AnimParams.h"

#include <QtGlobal>
#include <array>
#include <string>
#include <vector>

namespace {

// Indexed by int(ParamId); order MUST match the enum. The localization
// quartets (ids >= LocFirst) are generated in paramDesc(), not listed here.
const std::array<ParamDesc, int(ParamId::LocFirst)> kDescs = {{
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

    { "Position X",         -1,     1,  false, ParamScope::AllLayers },
    { "Position Y",         -1,     1,  false, ParamScope::AllLayers },
    { "Scale",              10,  1000,  false, ParamScope::AllLayers },
    { "Rotation",         -180,   180,  false, ParamScope::AllLayers },

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

    { "Pixel size",         1,   100,  true,  ParamScope::Dither },
    { "Strength",           0,   100,  true,  ParamScope::Dither },
    { "Opacity",            0,     1,  false, ParamScope::Dither },
    { "Corner radius",      0,   100,  false, ParamScope::Dither },
    { "Threshold",          0,   100,  true,  ParamScope::Dither },
    { "Levels",             2,    16,  true,  ParamScope::Dither },
    { "Line angle",         0,   180,  false, ParamScope::Dither },
    { "Line spacing",       2,    32,  true,  ParamScope::Dither },

    { "Cell size",          4,   100,  true,  ParamScope::Ascii },
    { "Gamma",            0.1,   5.0,  false, ParamScope::Ascii },
    { "Edges",              0,   100,  true,  ParamScope::Ascii },
    { "Hatching",            0,   100,  true,  ParamScope::Ascii },
    { "Stipple",             0,   100,  true,  ParamScope::Ascii },
    { "Contour",             0,   100,  true,  ParamScope::Ascii },
    { "Opacity",             0,     1,  false, ParamScope::Ascii },

    { "Spacing",             2,   200,  false, ParamScope::Mosaic },
    { "Width %",            10,   300,  false, ParamScope::Mosaic },
    { "Height %",           10,   300,  false, ParamScope::Mosaic },
    { "Text padding",        0,    45,  true,  ParamScope::Mosaic },
    { "Grid rotation",       0,   360,  false, ParamScope::Mosaic },

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
        case LayerKind::Mosaic:   return &l.mosaic.tonal;
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

// Display name of each LocParam; order MUST match the LocParam enum.
const char* kLocLabels[int(LocParam::Count)] = {
    "Diameter", "Gamma", "Weight", "Jitter", "Mask",
    "Strength", "Threshold", "Levels", "Line angle", "Line spacing", "Mask",
    "Gamma", "Edges", "Hatching", "Stipple", "Contour", "Mask",
    "Spacing", "Width %", "Height %", "Text padding", "Mask",
};

ParamScope locScope(LocParam p)
{
    switch (locParamKind(p)) {
        case LayerKind::Dither: return ParamScope::Dither;
        case LayerKind::Ascii:  return ParamScope::Ascii;
        case LayerKind::Mosaic: return ParamScope::Mosaic;
        default:                return ParamScope::Halftone;
    }
}

// The map holding LocParam p's point on this layer.
const LocMap& locMapFor(const Layer& l, LocParam p)
{
    switch (locParamKind(p)) {
        case LayerKind::Dither: return l.dither.loc;
        case LayerKind::Ascii:  return l.ascii.loc;
        case LayerKind::Mosaic: return l.mosaic.loc;
        default:                return l.halftone.loc;
    }
}
LocMap& locMapFor(Layer& l, LocParam p)
{
    return const_cast<LocMap&>(locMapFor(const_cast<const Layer&>(l), p));
}

} // namespace

const char* locParamLabel(LocParam p)
{
    return kLocLabels[int(p)];
}

const ParamDesc& paramDesc(ParamId id)
{
    const int li = locIndexOf(id);
    if (li < 0) return kDescs[size_t(id)];

    // Loc quartets are uniform; generate their descriptors once. Labels need
    // their own storage since ParamDesc only holds a const char*.
    static const std::vector<std::string> labels = [] {
        static const char* comp[4] = { " loc X", " loc Y", " loc rotation", " loc scale" };
        std::vector<std::string> v;
        for (int p = 0; p < int(LocParam::Count); ++p)
            for (int c = 0; c < 4; ++c)
                v.push_back(std::string(kLocLabels[p]) + comp[c]);
        return v;
    }();
    static const std::vector<ParamDesc> descs = [] {
        // Ranges mirror LocPoint's fields (posX/posY 0..1, rotation ±180,
        // scale 0.1..10).
        static const double lo[4] = { 0, 0, -180, 0.1 };
        static const double hi[4] = { 1, 1,  180, 10  };
        std::vector<ParamDesc> v;
        for (int p = 0; p < int(LocParam::Count); ++p)
            for (int c = 0; c < 4; ++c)
                v.push_back({ labels[size_t(p * 4 + c)].c_str(),
                              lo[c], hi[c], false, locScope(LocParam(p)) });
        return v;
    }();
    return descs[size_t(li)];
}

double getParam(const Layer& l, ParamId id)
{
    if (const int li = locIndexOf(id); li >= 0) {
        const LocParam p  = LocParam(li / 4);
        const LocPoint pt = locPointOr(locMapFor(l, p), p);
        switch (li & 3) {
            case 0:  return pt.posX;
            case 1:  return pt.posY;
            case 2:  return pt.rotation;
            default: return pt.scale;
        }
    }
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

        case ParamId::TfX:        return l.transform.xPct;
        case ParamId::TfY:        return l.transform.yPct;
        case ParamId::TfScale:    return l.transform.scalePct;
        case ParamId::TfRotation: return l.transform.rotation;

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
        case ParamId::AsHatching: return l.ascii.hatching;
        case ParamId::AsStipple:  return l.ascii.stipple;
        case ParamId::AsContour:  return l.ascii.contour;
        case ParamId::AsOpacity:  return l.ascii.opacity;

        case ParamId::MsSpacing:     return l.mosaic.spacing;
        case ParamId::MsWidthPct:    return l.mosaic.widthPct;
        case ParamId::MsHeightPct:   return l.mosaic.heightPct;
        case ParamId::MsTextPadding: return l.mosaic.textPadding;
        case ParamId::MsGridRotation: return l.mosaic.gridRotation;

        default: return 0.0;   // Document params handled elsewhere
    }
}

void setParam(Layer& l, ParamId id, double v)
{
    const ParamDesc& d = paramDesc(id);
    v = qBound(d.lo, v, d.hi);
    const int   iv = qRound(v);
    const float fv = float(v);

    if (const int li = locIndexOf(id); li >= 0) {
        const LocParam p  = LocParam(li / 4);
        LocPoint&      pt = locMapFor(l, p)[p];
        switch (li & 3) {
            case 0:  pt.posX     = fv; break;
            case 1:  pt.posY     = fv; break;
            case 2:  pt.rotation = fv; break;
            default: pt.scale    = fv; break;
        }
        return;
    }
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

        case ParamId::TfX:        l.transform.xPct     = fv; break;
        case ParamId::TfY:        l.transform.yPct     = fv; break;
        case ParamId::TfScale:    l.transform.scalePct = fv; break;
        case ParamId::TfRotation: l.transform.rotation = fv; break;

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
        case ParamId::AsHatching: l.ascii.hatching = iv; break;
        case ParamId::AsStipple:  l.ascii.stipple  = iv; break;
        case ParamId::AsContour:  l.ascii.contour  = iv; break;
        case ParamId::AsOpacity:  l.ascii.opacity  = fv; break;

        case ParamId::MsSpacing:     l.mosaic.spacing     = fv; break;
        case ParamId::MsWidthPct:    l.mosaic.widthPct    = fv; break;
        case ParamId::MsHeightPct:   l.mosaic.heightPct   = fv; break;
        case ParamId::MsTextPadding: l.mosaic.textPadding = iv; break;
        case ParamId::MsGridRotation: l.mosaic.gridRotation = fv; break;

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
        case LayerKind::Mosaic:   kindScope = ParamScope::Mosaic;  break;
        case LayerKind::Original: kindScope = ParamScope::AllLayers; break;  // adjustments only
    }

    std::vector<ParamId> out;
    for (int i = 0; i < int(ParamId::Count); ++i) {
        const ParamId id = ParamId(i);
        if (id == ParamId::HtInputDpi) continue;   // no UI control; ModePanel hardcodes it to 300
        // Loc quartets only make sense while their point exists and is on.
        if (const int li = locIndexOf(id); li >= 0) {
            const LocParam p = LocParam(li / 4);
            if (!locPointOr(locMapFor(layer, p), p).enabled) continue;
        }
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
