#pragma once

#include <QColor>
#include <QString>
#include <vector>

enum class HalftoneShape {
    Triangle,
    Circle,
    Square,
    Star,
    CustomSVG
};

struct ShapeEntry {
    HalftoneShape shape   = HalftoneShape::Circle;
    QString       svgPath;
};

struct FillEntry {
    QColor color   = QColor(0xD9, 0xD9, 0xD9);
    float  opacity = 1.0f;
};

struct HalftoneParams {
    // Shapes: 1-4 active slots
    std::vector<ShapeEntry> shapes         = { ShapeEntry{} };
    int                     multiThreshold = 128; // luminosity split [0-255], only when shapes.size() > 1

    // Grid
    int   gridSize     = 20;

    // Symbol transform
    float gamma        = 1.0f;
    float symbolSize   = 1.0f;
    float jitter       = 0.0f;
    float opacity      = 1.0f;
    float cornerRadius = 0.0f;

    // Fill: 1+ active slots
    bool                    useImageColors = false;
    std::vector<FillEntry> fills           = { FillEntry{} };

    // Stroke
    bool   strokeEnabled = false;
    float  strokeWidth   = 1.0f;
    QColor strokeColor   = Qt::black;
};
