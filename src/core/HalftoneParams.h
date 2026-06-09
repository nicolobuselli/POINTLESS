#pragma once

#include <QColor>
#include <QString>
#include <QImage>
#include <array>

enum class HalftoneShape {
    Circle,
    Square,
    Star,
    Spark,
    CrossX,
    Plus,
    CustomSVG
};

struct SymbolSlot {
    HalftoneShape shape     = HalftoneShape::Circle;
    QString       svgPath;          // used when shape == CustomSVG
    int           threshold = 0;    // luminosity lower bound [0-255]
};

struct HalftoneParams {
    // Grid
    int   gridSize    = 20;         // pixels per cell

    // Symbol scaling
    float gamma       = 1.0f;       // luminosity -> size exponent
    float symbolSize  = 1.0f;       // global scale multiplier

    // Jitter (rotation)
    float jitter      = 0.0f;       // [0-1], 1 = up to 360° random rotation

    // Opacity
    float opacity     = 1.0f;

    // Shape
    HalftoneShape shape = HalftoneShape::Circle;
    QString       customSvgPath;

    // Stroke
    bool  strokeEnabled = false;
    float strokeWidth   = 1.0f;
    float strokeRadius  = 0.0f;
    QColor strokeColor  = Qt::black;

    // Fill color
    QColor fillColor    = Qt::black;
    bool   useImageColors = false;

    // Multi-symbol mode
    bool multiSymbolEnabled = false;
    std::array<SymbolSlot, 4> symbolSlots;

    HalftoneParams() {
        // Default thresholds: 0, 64, 128, 192
        for (int i = 0; i < 4; ++i) {
            symbolSlots[i].threshold = i * 64;
        }
    }
};
