#pragma once

#include "HalftoneParams.h"
#include <QImage>
#include <QPainter>
#include <QPainterPath>

/**
 * HalftoneRenderer
 *
 * Pure rendering logic, independent from any UI class.
 * Uses QtConcurrent to parallelize rendering across rows.
 */
class HalftoneRenderer
{
public:
    HalftoneRenderer() = default;

    void render(const QImage& input, QPainter& output, const HalftoneParams& params);

private:
    // Per-row rendering (called from worker threads)
    struct RowJob {
        const QImage*         input;
        QImage*               canvas;
        const HalftoneParams* params;
        int                   row;
        int                   totalCols;
        int                   gs;
    };
    static void renderRow(const RowJob& job);

    // Shape builders
    static QPainterPath buildCircle(float cx, float cy, float r);
    static QPainterPath buildSquare(float cx, float cy, float r);
    static QPainterPath buildStar  (float cx, float cy, float r, int points = 5);
    static QPainterPath buildSpark (float cx, float cy, float r);
    static QPainterPath buildCrossX(float cx, float cy, float r);
    static QPainterPath buildPlus  (float cx, float cy, float r);

    static QPainterPath buildShape(HalftoneShape shape, float cx, float cy, float r);

    // Sampling
    static float  sampleLuminosity(const QImage& img, int x, int y, int w, int h);
    static QColor sampleAverageColor(const QImage& img, int x, int y, int w, int h);
    static unsigned int cellSeed(int col, int row);
};
