// Regression check: a raster document render must use frame dimensions and
// crop an oversized source in frame space instead of returning the source.
#include "workers/RenderWorker.h"

#include <QGuiApplication>
#include <QImage>
#include <cstdio>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    QImage source(200, 100, QImage::Format_ARGB32);
    for (int y = 0; y < source.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(source.scanLine(y));
        for (int x = 0; x < source.width(); ++x)
            line[x] = qRgb(x, y, 17);
    }

    SessionParams params;
    params.layers.clear();
    params.frameW = 80;
    params.frameH = 40;

    Layer layer;
    layer.id = 1;
    layer.kind = LayerKind::Original;
    layer.visible = true;
    params.layers.push_back(layer);

    const QImage rendered = RenderWorker::renderDocument(source, params);
    if (rendered.size() != QSize(params.frameW, params.frameH)) {
        std::fprintf(stderr, "wrong export size: %dx%d\n", rendered.width(), rendered.height());
        return 1;
    }

    // Default placement centres the native image. The 80x40 frame therefore
    // starts at source pixel (60,30).
    if (rendered.pixel(0, 0) != source.pixel(60, 30)
        || rendered.pixel(79, 39) != source.pixel(139, 69)) {
        std::fprintf(stderr, "source was not cropped through the document frame\n");
        return 2;
    }

    return 0;
}
