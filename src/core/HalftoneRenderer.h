#pragma once

#include "Params.h"
#include "GridGenerator.h"
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <vector>

// ============================================================
//  HalftoneRenderer
//
//  Consumes sample positions from the GridGenerator (square /
//  hexagonal / radial / line / circles) and draws a primitive at
//  each: a shape sized by local luminance for dot grids, or a
//  variable-width stroke connecting samples for Line / Circles.
//
//  Rendering is split into horizontal tiles (bands) rendered in
//  parallel and composited without overlap.
// ============================================================

class HalftoneRenderer
{
public:
    HalftoneRenderer() = default;

    void render(const QImage& input, QPainter& output, const HalftoneSettings& params);

private:
    struct TileJob {
        const QImage*                  inputRGB;
        const HalftoneSettings*        params;
        const std::vector<GridSample>* samples;
        QImage*                        canvas;     // band image (imgW × bandH)
        int                            bandTop;
        int                            bandH;
        int                            imgW;
        int                            imgH;
    };
    static void renderTile(const TileJob& job);
    static void paintDots   (QPainter& painter, const TileJob& job);
    static void paintStrokes(QPainter& painter, const TileJob& job);

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
    static void   cellAround(float sx, float sy, int cellPx, int imgW, int imgH,
                             int& cx, int& cy, int& cw, int& ch);
    static unsigned int cellSeed(int col, int row);
};
