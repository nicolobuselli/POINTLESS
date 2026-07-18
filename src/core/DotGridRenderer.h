#pragma once

#include "Params.h"
#include "GridGenerator.h"
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <vector>

// ============================================================
//  DotGridRenderer
//
//  Consumes sample positions from the GridGenerator (square /
//  hexagonal / radial) and draws a shape at each, sized by the
//  local luminance.
//
//  Rendering is split into horizontal tiles (bands) rendered in
//  parallel and composited without overlap.
// ============================================================

class DotGridRenderer
{
public:
    DotGridRenderer() = default;

    void render(const QImage& input, QPainter& output, const DotGridSettings& params);

    // Paint the dots straight onto `output` as vector paths (no parallel band
    // rasters), so an SVG export keeps real shapes. Same geometry as render().
    void renderVector(const QImage& input, QPainter& output, const DotGridSettings& params);

    // Upper bound on the dots this would draw (grid sample count), for the
    // export "heavy render" estimate — cheap, ignores tone culling.
    static int estimateDotCount(const QImage& input, const DotGridSettings& params);

    // GPU path (preview): true when every configured shape has an analytic
    // SDF in dot.frag (everything except CustomSVG).
    static bool gpuRenderable(const DotGridSettings& params);

    // shapeId as encoded for dot.frag: 0 circle, 1 square, 2 triangle, 3 star.
    static float gpuShapeId(DotGridShape s);

    // Three deterministic per-cell uniforms in [0,1) for jitter (rotation,
    // pivot angle, pivot distance). dot.vert ports the same hash bit-exactly
    // so GPU preview and CPU export land the symbols identically.
    static void cellJitterRands(int col, int row, float out[3]);

private:
    struct TileJob {
        const QImage*                  inputRGB;
        const DotGridSettings*        params;
        const std::vector<GridSample>* samples;
        QImage*                        canvas;     // band image (imgW × bandH)
        int                            bandTop;
        int                            bandH;
        int                            imgW;
        int                            imgH;
    };
    static void renderTile(const TileJob& job);
    static void paintDots  (QPainter& painter, const TileJob& job);

    // Shape builders
    static QPainterPath buildTriangle(float cx, float cy, float r, float cornerRadius = 0.f);
    static QPainterPath buildCircle  (float cx, float cy, float r);
    static QPainterPath buildSquare  (float cx, float cy, float r, float cornerRadius = 0.f);
    static QPainterPath buildStar    (float cx, float cy, float r, float cornerRadius = 0.f,
                                      int points = 5);
    // Closed polygon through `pts` with corners rounded by `radius` (0 = sharp).
    static QPainterPath roundedPolygon(const QVector<QPointF>& pts, float radius);

    static QPainterPath buildShape(DotGridShape shape, float cx, float cy, float r,
                                   float cornerRadius = 0.f);

    // Sampling
    static float  sampleLuminosity  (const QImage& rgbImg, int x, int y, int w, int h);
    static QColor sampleAverageColor(const QImage& rgbImg, int x, int y, int w, int h);
    static void   cellAround(float sx, float sy, int cellPx, int imgW, int imgH,
                             int& cx, int& cy, int& cw, int& ch);
    static unsigned int cellSeed(int col, int row);
};
