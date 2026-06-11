#pragma once

#include "HalftoneParams.h"
#include <QImage>
#include <QPainter>
#include <QPainterPath>

class HalftoneRenderer
{
public:
    HalftoneRenderer() = default;

    void render(const QImage& input, QPainter& output, const HalftoneParams& params);

private:
    struct RowJob {
        const QImage*         input;
        const QImage*         inputRGB;
        QImage*               canvas;
        const HalftoneParams* params;
        int                   row;
        int                   totalCols;
        int                   gs;
        int                   padding;
    };
    static void renderRow(const RowJob& job);

    // Shape builders
    static QPainterPath buildTriangle(float cx, float cy, float r);
    static QPainterPath buildCircle  (float cx, float cy, float r);
    static QPainterPath buildSquare  (float cx, float cy, float r, float cornerRadius = 0.f);
    static QPainterPath buildStar    (float cx, float cy, float r, int points = 5);

    static QPainterPath buildShape(HalftoneShape shape, float cx, float cy, float r,
                                   float cornerRadius = 0.f);

    // Sampling
    static float  sampleLuminosity  (const QImage& rgbImg, int x, int y, int w, int h);
    static QColor sampleAverageColor(const QImage& rgbImg, int x, int y, int w, int h);
    static unsigned int cellSeed(int col, int row);
};
