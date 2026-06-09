#pragma once
#include <QImage>
#include <QPainter>
#include "core/symbol.h"
#include <vector>

struct HalftoneParams {
    int gridSize = 20;
    double gamma = 1.0;
    double jitter = 0.0;
    double opacity = 1.0;
    double symbolSize = 1.0;
    bool enableStroke = false;
    double strokeWidth = 1.0;
    double strokeRadius = 0.0;
    QColor strokeColor = Qt::black;
    QColor fillColor = Qt::white;
    bool useImageColors = false;
    bool multiSymbolMode = false;
    std::vector<Symbol> symbols;
    std::vector<int> thresholds;
};

class HalftoneRenderer {
public:
    static void render(const QImage& input, QImage& output, const HalftoneParams& params);
};
