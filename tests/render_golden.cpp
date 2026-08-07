// Renders a fixed set of configurations to PNG so two builds of the core can
// be diffed pixel-for-pixel. Same binary source both sides; only the core/
// include path differs.
#include "core/AsciiRenderer.h"
#include "core/DitherRenderer.h"
#include "core/DotGridRenderer.h"
#include "core/HalftoneRenderer.h"
#include "core/ImageAdjuster.h"
#include "core/MosaicRenderer.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <cstdio>

static QImage source(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            // Gradients + structure, so tone selection actually varies.
            const int r = (x * 255) / w;
            const int g = (y * 255) / h;
            const int b = ((x / 17 + y / 13) * 37) & 0xFF;
            line[x] = qRgba(r, g, b, 255);
        }
    }
    return img;
}

template <typename F>
static void shot(const QString& dir, const char* name, QSize sz, F&& draw)
{
    QImage out(sz, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    { QPainter p(&out); p.setRenderHint(QPainter::Antialiasing, true);
      p.setRenderHint(QPainter::TextAntialiasing, true); draw(p); }
    out.save(dir + "/" + name + ".png");
    std::printf("  %s\n", name);
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const QString dir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");

    const QSize sz(900, 600);
    const QImage src = source(sz.width(), sz.height());

    // Tone sets that exercise both the fixed-tone and image-colour branches.
    TonalSettings fixed;
    fixed.mode = ToneMode::FixedTones;
    fixed.tones = { { QColor(20, 20, 30), 40, 1.0f },
                    { QColor(200, 60, 40), 128, 1.0f },
                    { QColor(240, 230, 200), 220, 1.0f } };
    TonalSettings imageCols;
    imageCols.mode = ToneMode::ImageColors;

    shot(dir, "dotgrid_fixed", sz, [&](QPainter& p) {
        DotGridSettings s; s.tonal = fixed; s.grid.spacing = 9.0f;
        DotGridRenderer r; r.render(src, p, s);
    });
    shot(dir, "dotgrid_imagecolors", sz, [&](QPainter& p) {
        DotGridSettings s; s.tonal = imageCols; s.grid.spacing = 7.0f;
        s.grid.type = GridType::Hexagonal;
        DotGridRenderer r; r.render(src, p, s);
    });
    shot(dir, "ascii_fixed", sz, [&](QPainter& p) {
        AsciiSettings s; s.tonal = fixed; s.cellSize = 12;
        AsciiRenderer::render(src, p, s);
    });
    shot(dir, "ascii_imagecolors_effects", sz, [&](QPainter& p) {
        AsciiSettings s; s.tonal = imageCols; s.cellSize = 10;
        s.edges = 40; s.hatching = 30; s.stipple = 25; s.contour = 0;
        AsciiRenderer::render(src, p, s);
    });
    shot(dir, "ascii_nonsquare_grid", sz, [&](QPainter& p) {
        AsciiSettings s; s.tonal = fixed; s.cellSize = 14;
        s.gridShape = GridType::Hexagonal;
        AsciiRenderer::render(src, p, s);
    });
    shot(dir, "mosaic_fixed", sz, [&](QPainter& p) {
        MosaicSettings s; s.tonal = fixed; s.spacing = 22.0f;
        MosaicRenderer::render(src, p, s);
    });
    shot(dir, "mosaic_imagecolors", sz, [&](QPainter& p) {
        MosaicSettings s; s.tonal = imageCols; s.spacing = 16.0f;
        MosaicRenderer::render(src, p, s);
    });
    shot(dir, "halftone_cmyk", sz, [&](QPainter& p) {
        HalftoneSettings s; s.tonal = imageCols; s.spacing = 8.0f;
        HalftoneRenderer r; r.render(src, p, s);
    });
    shot(dir, "halftone_tonal", sz, [&](QPainter& p) {
        HalftoneSettings s; s.tonal = fixed; s.spacing = 10.0f;
        HalftoneRenderer r; r.render(src, p, s);
    });
    shot(dir, "halftone_ink", sz, [&](QPainter& p) {
        HalftoneSettings s; s.tonal = imageCols; s.spacing = 12.0f;
        s.dotShape = ScreenDotShape::Ink; s.grain = 0.4f; s.softness = 0.3f;
        HalftoneRenderer r; r.render(src, p, s);
    });
    shot(dir, "dither_bayer", sz, [&](QPainter& p) {
        DitherSettings s; s.tonal = fixed; s.algorithm = DitherAlgorithm::Bayer;
        p.drawImage(0, 0, DitherRenderer::render(src, s));
    });
    shot(dir, "dither_floyd", sz, [&](QPainter& p) {
        DitherSettings s; s.tonal = fixed; s.algorithm = DitherAlgorithm::FloydSteinberg;
        p.drawImage(0, 0, DitherRenderer::render(src, s));
    });
    shot(dir, "dither_linehatch", sz, [&](QPainter& p) {
        DitherSettings s; s.tonal = fixed; s.algorithm = DitherAlgorithm::LineHatch;
        s.lineAngle = 27.0f; s.lineSpacing = 6;
        p.drawImage(0, 0, DitherRenderer::render(src, s));
    });
    shot(dir, "adjust_chain", sz, [&](QPainter& p) {
        Adjustments a;
        a.brightness = 12; a.contrast = 20; a.gamma = 120;
        a.levelsBlack = 10; a.levelsWhite = 245; a.saturation = 30;
        a.invert = true; a.blur = 6; a.grain = 15; a.posterize = 12;
        p.drawImage(0, 0, ImageAdjuster::apply(src, a));
    });
    return 0;
}
