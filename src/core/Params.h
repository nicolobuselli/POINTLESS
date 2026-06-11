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
    FixedTones  = 1
};

struct ToneEntry {
    QColor color = QColor(0xD9, 0xD9, 0xD9);
    int    level = 128;   // 0..255 luminosity anchor
};

inline bool operator==(const ToneEntry& a, const ToneEntry& b) {
    return a.color == b.color && a.level == b.level;
}

struct TonalSettings {
    ToneMode               mode = ToneMode::FixedTones;
    std::vector<ToneEntry> tones;   // UI order: light → dark
};

inline bool operator==(const TonalSettings& a, const TonalSettings& b) {
    return a.mode == b.mode && a.tones == b.tones;
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

struct HalftoneSettings {
    int                     inputDpi       = 72;   // 18..300 — render resolution
    std::vector<ShapeEntry> shapes         = { ShapeEntry{} };
    int                     multiThreshold = 128;  // luminosity bias, shapes.size() > 1

    int   gridSize     = 20;
    float gamma        = 1.0f;
    float symbolSize   = 1.0f;
    float jitter       = 0.0f;
    float opacity      = 1.0f;
    float cornerRadius = 0.0f;

    TonalSettings tonal { ToneMode::FixedTones, defaultAccentTones(1) };
};

inline bool operator==(const HalftoneSettings& a, const HalftoneSettings& b) {
    return a.inputDpi == b.inputDpi && a.shapes == b.shapes
        && a.multiThreshold == b.multiThreshold && a.gridSize == b.gridSize
        && a.gamma == b.gamma && a.symbolSize == b.symbolSize
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
    DotDiffusion        = 12   // Class-matrix ordered + error diffusion
};

struct DitherSettings {
    DitherAlgorithm algorithm = DitherAlgorithm::FloydSteinberg;
    int             bayerSize = 8;    // 2, 4, 8, 16
    int             pixelSize = 2;    // 1..16 — chunky pixels
    int             strength  = 100;  // 0..100
    int             levels    = 2;    // 2..8 per channel (image colors mode)

    TonalSettings tonal { ToneMode::FixedTones, defaultAccentTones(1) };
};

inline bool operator==(const DitherSettings& a, const DitherSettings& b) {
    return a.algorithm == b.algorithm && a.bayerSize == b.bayerSize
        && a.pixelSize == b.pixelSize && a.strength == b.strength
        && a.levels == b.levels && a.tonal == b.tonal;
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
    };
    return presets;
}

struct AsciiSettings {
    int     charsetPreset = 0;        // index into presets; == size() → custom
    QString customCharset;
    int     cellSize      = 12;       // 4..48 px
    float   gamma         = 1.0f;
    bool    invert        = false;

    TonalSettings tonal { ToneMode::FixedTones,
                          { ToneEntry{ QColor(0xC0, 0xC0, 0xC0), 0 } } };

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
        && a.invert == b.invert && a.tonal == b.tonal;
}

// ============================================================
//  Whole document state (used for rendering and undo/redo)
// ============================================================

struct SessionParams {
    Adjustments      adjustments;
    RenderMode       mode = RenderMode::Halftone;
    HalftoneSettings halftone;
    DitherSettings   dither;
    AsciiSettings    ascii;

    QColor background        = QColor(0x0A, 0x0A, 0x0A);
    float  backgroundOpacity = 1.0f;
};

inline bool operator==(const SessionParams& a, const SessionParams& b) {
    return a.adjustments == b.adjustments && a.mode == b.mode
        && a.halftone == b.halftone && a.dither == b.dither && a.ascii == b.ascii
        && a.background == b.background && a.backgroundOpacity == b.backgroundOpacity;
}
inline bool operator!=(const SessionParams& a, const SessionParams& b) { return !(a == b); }
