#pragma once

#include <QColor>
#include <QString>
#include <vector>

// ============================================================
//  Render mode
// ============================================================

enum class RenderMode {
    Halftone = 0,
    Dither   = 1,
    Ascii    = 2
};

// ============================================================
//  Layer kinds and blend modes
// ============================================================

enum class LayerKind {
    Original = 0,
    Halftone = 1,
    Dither   = 2,
    Ascii    = 3
};

// Classic Photoshop blend modes, in Photoshop menu order.
enum class BlendMode {
    Normal = 0, Dissolve,
    Darken, Multiply, ColorBurn, LinearBurn, DarkerColor,
    Lighten, Screen, ColorDodge, LinearDodge, LighterColor,
    Overlay, SoftLight, HardLight, VividLight, LinearLight, PinLight, HardMix,
    Difference, Exclusion, Subtract, Divide,
    Hue, Saturation, Color, Luminosity
};

inline LayerKind layerKindForMode(RenderMode m)
{
    switch (m) {
        case RenderMode::Dither: return LayerKind::Dither;
        case RenderMode::Ascii:  return LayerKind::Ascii;
        default:                 return LayerKind::Halftone;
    }
}

inline RenderMode modeForLayerKind(LayerKind k)
{
    switch (k) {
        case LayerKind::Dither: return RenderMode::Dither;
        case LayerKind::Ascii:  return RenderMode::Ascii;
        default:                return RenderMode::Halftone;
    }
}

inline QString layerKindName(LayerKind k)
{
    switch (k) {
        case LayerKind::Original: return QStringLiteral("Original");
        case LayerKind::Halftone: return QStringLiteral("Halftone");
        case LayerKind::Dither:   return QStringLiteral("Dither");
        case LayerKind::Ascii:    return QStringLiteral("Ascii");
    }
    return {};
}

// ============================================================
//  Global image adjustments (left panel)
// ============================================================

struct Adjustments {
    int brightness      = 0;    // -100..100
    int contrast        = 0;    // -100..100
    int gamma           = 100;  // 10..300  → actual = value/100  (1.0 = neutral)
    int levelsBlack     = 0;    // 0..255   — clips input shadows
    int levelsMid       = 100;  // 10..500  → actual = value/100  (1.0 = neutral)
    int levelsWhite     = 255;  // 0..255   — clips input highlights
    int saturation      = 0;    // -100..100
    int sizePct         = 100;  // 10..200  — resamples the source
    int sharpenStrength = 0;    // 0..100
    int sharpenRadius   = 1;    // 1..10 px
    int edgeEnhancement = 0;    // 0..100
    int blur            = 0;    // 0..100
    int grain           = 0;    // 0..100   (was: noise)
    int posterize       = 256;  // 2..256   (256 = disabled)
    int threshold       = 0;    // 0..255   (0 = disabled)
};

inline bool operator==(const Adjustments& a, const Adjustments& b) {
    return a.brightness == b.brightness && a.contrast == b.contrast
        && a.gamma == b.gamma
        && a.levelsBlack == b.levelsBlack && a.levelsMid == b.levelsMid
        && a.levelsWhite == b.levelsWhite
        && a.saturation == b.saturation && a.sizePct == b.sizePct
        && a.sharpenStrength == b.sharpenStrength
        && a.sharpenRadius == b.sharpenRadius
        && a.edgeEnhancement == b.edgeEnhancement
        && a.blur == b.blur && a.grain == b.grain
        && a.posterize == b.posterize && a.threshold == b.threshold;
}
inline bool operator!=(const Adjustments& a, const Adjustments& b) { return !(a == b); }

// ============================================================
//  Tonal controls
//
//  Either the renderer samples colors from the image, or it maps
//  luminosity onto a list of tones. Each tone has a color and a
//  "level" anchor (0..255): a pixel/cell takes the color of the
//  tone whose level is nearest to its luminosity, so the sliders
//  shift where each tone dominates.
// ============================================================

enum class ToneMode {
    ImageColors = 0,
    FixedTones  = 1,
    Palette     = 2     // dither each pixel to the nearest tone colour
};

struct ToneEntry {
    QColor color = QColor(0xD9, 0xD9, 0xD9);
    int    level = 128;      // 0..255 luminosity anchor
    float  opacity = 1.0f;   // 0.0..1.0 per-tone opacity
};

inline bool operator==(const ToneEntry& a, const ToneEntry& b) {
    return a.color == b.color && a.level == b.level && a.opacity == b.opacity;
}

struct TonalSettings {
    ToneMode               mode = ToneMode::FixedTones;
    std::vector<ToneEntry> tones;   // UI order: light → dark
    bool                   enabled = true;   // Fill "−" removes the fill (false → render uses image colours)
};

inline bool operator==(const TonalSettings& a, const TonalSettings& b) {
    return a.mode == b.mode && a.tones == b.tones && a.enabled == b.enabled;
}

// Evenly spaced default tones, light → dark.
inline std::vector<ToneEntry> defaultTones(int n)
{
    std::vector<ToneEntry> out;
    if (n <= 1) {
        out.push_back({ QColor(0x1A, 0x1A, 0x1A), 0 });
        return out;
    }
    const QColor light(0xE8, 0xE8, 0xE8);
    const QColor dark (0x1A, 0x1A, 0x1A);
    for (int i = 0; i < n; ++i) {
        float t = float(i) / float(n - 1);
        QColor c(int(light.red()   + t * (dark.red()   - light.red())),
                 int(light.green() + t * (dark.green() - light.green())),
                 int(light.blue()  + t * (dark.blue()  - light.blue())));
        out.push_back({ c, 255 - qRound(t * 255.0f) });
    }
    return out;
}

inline std::vector<ToneEntry> defaultAccentTones(int n)
{
    std::vector<ToneEntry> out;
    if (n <= 1) {
        out.push_back({ QColor(0xFD, 0x5A, 0x1F), 0 });
        return out;
    }

    const QColor accent(0xFD, 0x5A, 0x1F);
    for (int i = 0; i < n; ++i) {
        const float t = float(i) / float(n - 1);
        out.push_back({ accent, 255 - qRound(t * 255.0f) });
    }
    return out;
}

// Index of the tone whose level is nearest to the given luminosity.
inline int pickToneIndex(const std::vector<ToneEntry>& tones, float lum01)
{
    if (tones.empty()) return -1;
    const int lum = qRound(lum01 * 255.0f);
    int best = 0, bestDist = 256 * 2;
    for (int i = 0; i < int(tones.size()); ++i) {
        int d = qAbs(lum - tones[i].level);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

// ── Preset palettes ───────────────────────────────────────────

struct PalettePreset {
    QString             name;
    std::vector<QColor> colors;   // light → dark
};

inline const std::vector<PalettePreset>& palettePresets()
{
    static const std::vector<PalettePreset> presets = {
        { "Ink & Paper", { QColor("#EDEAE0"), QColor("#1A1A1A") } },
        { "Gameboy",     { QColor("#9BBC0F"), QColor("#8BAC0F"), QColor("#306230"), QColor("#0F380F") } },
        { "Sepia",       { QColor("#F4E4C1"), QColor("#C8A165"), QColor("#7A5230") } },
        { "Ocean",       { QColor("#D7F5FF"), QColor("#4FA3D1"), QColor("#10316B") } },
        { "Neon",        { QColor("#00FFD1"), QColor("#FF2079"), QColor("#2B0A4E") } },
        { "Fire",        { QColor("#FFE08A"), QColor("#F95B1E"), QColor("#5C0A0A") } },
        { "CMYK",        { QColor("#FFF200"), QColor("#00AEEF"), QColor("#EC008C"), QColor("#231F20") } },
        { "Grayscale",   { QColor("#F2F2F2"), QColor("#BFBFBF"), QColor("#8C8C8C"), QColor("#595959"), QColor("#262626") } },
    };
    return presets;
}

// Build tones from a flat color list with evenly spaced levels.
inline std::vector<ToneEntry> tonesFromColors(const std::vector<QColor>& colors)
{
    std::vector<ToneEntry> out;
    const int n = int(colors.size());
    for (int i = 0; i < n; ++i) {
        int level = (n <= 1) ? 0 : 255 - qRound(float(i) * 255.0f / float(n - 1));
        out.push_back({ colors[i], level });
    }
    return out;
}

// ============================================================
//  Halftone
// ============================================================

enum class HalftoneShape {
    Triangle,
    Circle,
    Square,
    Star,
    CustomSVG
};

struct ShapeEntry {
    HalftoneShape shape = HalftoneShape::Circle;
    QString       svgPath;
};

inline bool operator==(const ShapeEntry& a, const ShapeEntry& b) {
    return a.shape == b.shape && a.svgPath == b.svgPath;
}

// ── Grid system ──────────────────────────────────────────────
//
//  Decouples WHERE samples are placed from HOW primitives are drawn.
//  A generator produces transformed sample positions; the renderer
//  draws a luminance-sized shape at each.
//
//    spacing      — distance between grid elements (px)
//    pointSpacing — sample spacing within a ring (Radial)
//    diameter     — base primitive size multiplier (independent of spacing)
//    stretch*     — anisotropic scaling of the grid along an axis
//
enum class GridType {
    Square = 0,
    Hexagonal,
    Brick,
    Wave,
    Radial,
    Phyllotaxis
};

struct GridSettings {
    GridType type               = GridType::Square;
    float    spacing            = 50.0f;  // 2..200
    float    pointSpacing       = 20.0f;  // 2..200 — Radial ring sampling
    float    rotation           = 0.0f;   // 0..360 deg
    float    diameter           = 1.0f;   // 0.1..3.0 primitive size multiplier
    float    stretchFactor      = 1.0f;   // 0.1..4.0 anisotropic scale
    float    stretchAngle       = 0.0f;   // 0..360 deg
    bool     followGridRotation = false;
};

inline bool operator==(const GridSettings& a, const GridSettings& b) {
    return a.type == b.type && a.spacing == b.spacing
        && a.pointSpacing == b.pointSpacing && a.rotation == b.rotation
        && a.diameter == b.diameter && a.stretchFactor == b.stretchFactor
        && a.stretchAngle == b.stretchAngle
        && a.followGridRotation == b.followGridRotation;
}

struct HalftoneSettings {
    int                     inputDpi       = 72;   // 18..300 — render resolution
    std::vector<ShapeEntry> shapes         = { ShapeEntry{} };
    int                     multiThreshold = 128;  // luminosity bias, shapes.size() > 1

    GridSettings grid;
    float gamma        = 1.0f;
    float weight       = 0.0f;   // 0 = size ∝ ink coverage, 1 = all symbols full size
    float jitter       = 0.0f;
    float opacity      = 1.0f;
    float cornerRadius = 0.0f;

    TonalSettings tonal { ToneMode::FixedTones, defaultAccentTones(1) };
};

inline bool operator==(const HalftoneSettings& a, const HalftoneSettings& b) {
    return a.inputDpi == b.inputDpi && a.shapes == b.shapes
        && a.multiThreshold == b.multiThreshold && a.grid == b.grid
        && a.gamma == b.gamma && a.weight == b.weight
        && a.jitter == b.jitter && a.opacity == b.opacity
        && a.cornerRadius == b.cornerRadius && a.tonal == b.tonal;
}

// ============================================================
//  Dither
// ============================================================

enum class DitherAlgorithm {
    // ── Error Diffusion ──────────────────────────────────────────
    FloydSteinberg      = 0,   // Classic; balanced worm artifacts
    FalseFloydSteinberg = 1,   // Lightweight FS approximation; very fast
    Atkinson            = 2,   // 75 % error; iconic retro / Mac aesthetic
    Burkes              = 3,   // Simplified JJN; reduced directional bias
    Sierra              = 4,   // Refined diffusion; smooth gradients
    SierraLite          = 5,   // Minimal Sierra; real-time friendly
    JarvisJudiceNinke   = 6,   // Wide kernel; exceptional tonal accuracy
    Stucki              = 7,   // JJN variant; cleaner shadows
    // ── Ordered Dithering ────────────────────────────────────────
    Bayer               = 8,   // Threshold matrix; crisp geometric pattern
    ClusteredDot        = 9,   // Halftone-style dot clusters; print-like
    BlueNoise           = 10,  // Void-and-cluster mask; natural appearance
    VoidAndCluster      = 11,  // Ulichney optimal mask; minimal repetition
    // ── Hybrid ───────────────────────────────────────────────────
    DotDiffusion        = 12,  // Class-matrix ordered + error diffusion
    // ── Tone ──────────────────────────────────────────────────────
    Threshold           = 13,  // Hard B/W cut by a threshold (no dithering)
    // ── Error Diffusion (extended; tables/refs from libdither) ────
    Ostromoukhov        = 14,  // Variable-coefficient error diffusion
    Riemersma           = 15,  // Error diffusion along a Hilbert curve
    // ── Ordered (extended) ───────────────────────────────────────
    LineHatch           = 16,  // Parallel line screen; thickness follows tone
    CustomPattern       = 17   // User image tiled as the threshold matrix
};

struct DitherSettings {
    DitherAlgorithm algorithm = DitherAlgorithm::FloydSteinberg;
    int             bayerSize = 8;    // 2, 4, 8, 16
    int             pixelSize = 2;    // 1..16 — chunky pixels
    int             strength  = 50;   // 0..100
    int             threshold = 50;   // 0..100 — only for the Threshold algorithm
    float           opacity      = 1.0f;
    float           cornerRadius = 0.0f;
    int             levels       = 2;     // 2..16 — mixing inks: steps/channel (image colors) or interpolated tones (fills)
    bool            serpentine   = true;  // error diffusion: zig-zag scan vs left→right
    float           lineAngle    = 45.0f; // 0..180 deg — LineHatch direction
    int             lineSpacing  = 6;     // 2..32 cells — LineHatch line period
    QString         patternPath;          // CustomPattern threshold image

    TonalSettings tonal { ToneMode::FixedTones, defaultAccentTones(1) };
};

inline bool operator==(const DitherSettings& a, const DitherSettings& b) {
    return a.algorithm == b.algorithm && a.bayerSize == b.bayerSize
        && a.pixelSize == b.pixelSize && a.strength == b.strength
        && a.threshold == b.threshold
        && a.opacity == b.opacity && a.cornerRadius == b.cornerRadius
        && a.levels == b.levels && a.serpentine == b.serpentine
        && a.lineAngle == b.lineAngle && a.lineSpacing == b.lineSpacing
        && a.patternPath == b.patternPath
        && a.tonal == b.tonal;
}

// ============================================================
//  Ascii
// ============================================================

struct AsciiCharsetPreset {
    QString name;
    QString chars;   // lightest → darkest
};

inline const std::vector<AsciiCharsetPreset>& asciiCharsetPresets()
{
    static const std::vector<AsciiCharsetPreset> presets = {
        { "Standard", QString::fromUtf8(" .:-=+*#%@") },
        { "Detailed", QString::fromUtf8(" .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$") },
        { "Blocks",   QString::fromUtf8(" ░▒▓█") },
        { "Minimal",  QString::fromUtf8(" .oO0@") },
        { "Binary",   QString::fromUtf8(" 01") },
        // Special-cased by the renderer: 2×4 braille dots per cell (8× the
        // effective resolution). The ramp here is only a safe fallback.
        { "Braille",  QString::fromUtf8(" ⣿") },
    };
    return presets;
}

// The Braille preset is render-path special-cased; keep it LAST in the list.
inline int asciiBraillePreset() { return int(asciiCharsetPresets().size()) - 1; }

struct AsciiSettings {
    int     charsetPreset = 0;        // index into presets; == size() → custom
    QString customCharset;
    int     cellSize      = 12;       // 4..48 px
    float   gamma         = 1.0f;
    bool    invert        = false;
    QString fontFamily    = QStringLiteral("Consolas");
    int     fontWeight    = 600;      // QFont::Weight (400/500/600/700)
    int     edges         = 0;        // 0..100 — contour glyphs (/ - \ |); 0 = off
    bool    cellBackground = false;   // fill the cell with the ink, punch the glyph out

    TonalSettings tonal { ToneMode::FixedTones,
                          { ToneEntry{ QColor(0xC0, 0xC0, 0xC0), 0 } } };

    bool isBraille() const { return charsetPreset == asciiBraillePreset(); }

    QString effectiveCharset() const {
        const auto& presets = asciiCharsetPresets();
        if (charsetPreset >= 0 && charsetPreset < int(presets.size()))
            return presets[charsetPreset].chars;
        if (customCharset.size() >= 2) return customCharset;
        return presets[0].chars;
    }
};

inline bool operator==(const AsciiSettings& a, const AsciiSettings& b) {
    return a.charsetPreset == b.charsetPreset && a.customCharset == b.customCharset
        && a.cellSize == b.cellSize && a.gamma == b.gamma
        && a.invert == b.invert
        && a.fontFamily == b.fontFamily && a.fontWeight == b.fontWeight
        && a.edges == b.edges && a.cellBackground == b.cellBackground
        && a.tonal == b.tonal;
}

// ============================================================
//  Layer — one entry of the per-image layer stack
//
//  Each layer owns its full parameter set: its own image
//  adjustments (the layer's embedded "reference image" is
//  source + adjustments) plus the render settings of its kind.
//  Multiple layers of the same kind can coexist; layers are
//  identified by a per-session unique id.
// ============================================================

// Per-layer placement on the frame: centre offset as a fraction of the frame,
// a uniform scale (100% = the layer's own pixels drawn 1:1 on the frame) and a
// rotation. Animatable.
struct LayerTransform {
    float xPct     = 0.0f;     // -1..1 — horizontal centre offset (fraction of frame)
    float yPct     = 0.0f;     // -1..1 — vertical centre offset
    float scalePct = 100.0f;   // uniform scale (100 = native pixels)
    float rotation = 0.0f;     // 0..360 deg
    bool  flipH    = false;    // mirror about the vertical (y) axis — left/right
    bool  flipV    = false;    // mirror about the horizontal (x) axis — top/bottom
};
inline bool operator==(const LayerTransform& a, const LayerTransform& b) {
    return a.xPct == b.xPct && a.yPct == b.yPct
        && a.scalePct == b.scalePct && a.rotation == b.rotation
        && a.flipH == b.flipH && a.flipV == b.flipV;
}

// Scale a layer so it fits entirely inside the frame (contain), keeping aspect.
inline LayerTransform fitTransform(int imgW, int imgH, int frameW, int frameH)
{
    LayerTransform t;
    if (imgW > 0 && imgH > 0 && frameW > 0 && frameH > 0)
        t.scalePct = qMin(float(frameW) / imgW, float(frameH) / imgH) * 100.0f;
    return t;
}

struct Layer {
    int       id      = -1;
    LayerKind kind    = LayerKind::Original;
    QString   name;
    bool      visible = true;
    bool      pinned  = false;   // turned on by hand → survives mode switches
    BlendMode blend   = BlendMode::Normal;

    int            mediaId   = -1;   // which media this layer draws (-1 = document base)
    LayerTransform transform;        // placement on the canvas

    Adjustments      adjustments;   // per-layer reference image
    HalftoneSettings halftone;      // only the struct matching `kind` is used
    DitherSettings   dither;
    AsciiSettings    ascii;
};

inline bool operator==(const Layer& a, const Layer& b) {
    return a.id == b.id && a.kind == b.kind && a.name == b.name
        && a.visible == b.visible && a.pinned == b.pinned && a.blend == b.blend
        && a.mediaId == b.mediaId && a.transform == b.transform
        && a.adjustments == b.adjustments && a.halftone == b.halftone
        && a.dither == b.dither && a.ascii == b.ascii;
}
inline bool operator!=(const Layer& a, const Layer& b) { return !(a == b); }

inline int findLayerById(const std::vector<Layer>& layers, int id)
{
    for (int i = 0; i < int(layers.size()); ++i)
        if (layers[i].id == id) return i;
    return -1;
}

// Session default: active Halftone layer over the hidden Original.
inline std::vector<Layer> defaultLayers()
{
    Layer halftone;
    halftone.id      = 2;
    halftone.kind    = LayerKind::Halftone;
    halftone.name    = layerKindName(LayerKind::Halftone);
    halftone.visible = true;

    Layer original;
    original.id      = 1;
    original.kind    = LayerKind::Original;
    original.name    = layerKindName(LayerKind::Original);
    original.visible = false;

    return { halftone, original };
}

// ============================================================
//  Parent groups (cascade): a parent is a source image (a media
//  entry) that does NOT itself appear in the frame. Its children
//  are the rendering Layers whose mediaId == this group's mediaId.
//  Order in SessionParams::parents = top→bottom group order.
// ============================================================

struct ParentGroup {
    int     mediaId      = -1;
    QString name;
    bool    collapsed    = false;   // children hidden in the panel (UI only)
    bool    groupVisible = true;    // master visibility for all children in the frame
};
inline bool operator==(const ParentGroup& a, const ParentGroup& b) {
    return a.mediaId == b.mediaId && a.name == b.name
        && a.collapsed == b.collapsed && a.groupVisible == b.groupVisible;
}
inline bool operator!=(const ParentGroup& a, const ParentGroup& b) { return !(a == b); }

// ============================================================
//  Whole document state (used for rendering and undo/redo)
// ============================================================

struct SessionParams {
    std::vector<Layer>       layers  = defaultLayers();   // children, UI order: 0 = top
    std::vector<ParentGroup> parents;                     // source groups, top→bottom
    int                activeLayerId = 2;          // layer edited by the panels
    int                nextLayerId   = 3;          // id counter for new layers

    QColor background        = QColor(0x0A, 0x0A, 0x0A);
    float  backgroundOpacity = 1.0f;

    int    frameW = 1080;   // canvas the layers are composited onto
    int    frameH = 1080;
};

inline int findParentByMedia(const std::vector<ParentGroup>& parents, int mediaId) {
    for (int i = 0; i < int(parents.size()); ++i)
        if (parents[i].mediaId == mediaId) return i;
    return -1;
}

// activeLayerId is deliberately ignored: selection alone is not an
// undoable change.
inline bool operator==(const SessionParams& a, const SessionParams& b) {
    return a.layers == b.layers && a.parents == b.parents && a.nextLayerId == b.nextLayerId
        && a.background == b.background && a.backgroundOpacity == b.backgroundOpacity
        && a.frameW == b.frameW && a.frameH == b.frameH;
}
inline bool operator!=(const SessionParams& a, const SessionParams& b) { return !(a == b); }
