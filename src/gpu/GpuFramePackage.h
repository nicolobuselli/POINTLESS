#pragma once

#include <QImage>
#include <QTransform>
#include <QColor>
#include <QSize>
#include <QVector>
#include "../core/Params.h"

// ============================================================
//  GpuFramePackage — one displayable frame, pre-composite.
//
//  Produced by RenderWorker (per-layer CPU renders, cached as
//  today) and consumed by GpuCanvasWidget, which does placement
//  + blend-mode compositing on the GPU. `placement` maps layer
//  pixels → frame pixels (the exact placeOnFrame matrix).
// ============================================================

struct GpuLayer {
    int        id = -1;
    QImage     image;       // ARGB32_Premultiplied, layer-native raster
    QTransform placement;   // layer px → frame px
    BlendMode  blend = BlendMode::Normal;
    // Phase 2: Original layers with point-op-only adjustments upload the RAW
    // source and apply brightness/contrast/gamma/levels/saturation/invert/
    // grain/posterize/threshold in composite.frag (sizePct folds into
    // `placement`). Slider drags then touch only a UBO — no CPU re-render,
    // no texture re-upload.
    Adjustments adj;
    bool        gpuAdjust = false;

    // Phase 4: the GPU adjust chain. `image` is the RAW straight-alpha sRGB
    // source; GpuCanvasWidget runs the full ImageAdjuster pipeline on it as
    // fullscreen passes (point ops + box blur + edge + unsharp + finish) at
    // contentSize resolution (sizePct = the implicit resize of the first
    // pass). Set for every dot/halftone screen layer (the finish pass also
    // flattens onto white + linearizes + mips, replacing CPU toLinearF16)
    // and for Original layers with neighborhood ops (finish premultiplies;
    // the composite pass then treats the result as a plain raster).
    bool        adjustChain = false;

    // Dot Grid screen layer (fully uniform-driven, like halftone): `image`
    // holds the RAW SOURCE (see adjustChain); dot.vert derives every sample
    // position from gl_InstanceIndex (GridGenerator layout) and the cell
    // tone/colour from a textureLod on the mipmapped adjusted source, so ANY
    // Dot Grid control — spacing and grid layout included — is a UBO update.
    bool            dotScreen = false;
    // Effective settings for the UBO: the live layer's DotGridSettings with
    // grid.spacing already multiplied by the bake's compensation factor
    // (compensateSymbolScale + prerenderAtFrameRes), i.e. in contentSize px.
    DotGridSettings dotSettings;
    // Halftone screen layer (fully uniform-driven): `image` holds the RAW
    // SOURCE (see adjustChain — the chain produces the linear fp16 mipmapped
    // texture halftone.frag samples). `halftoneSettings` follows the same
    // spacing-compensation rule as dotSettings.
    bool             halftoneScreen = false;
    HalftoneSettings halftoneSettings;
    // Dither screen layer (ordered/threshold algorithms, uniform-driven):
    // dither.frag maps output pixels to chunky cells and thresholds the
    // adjust chain's linear source. `ditherSettings.pixelSize` follows the
    // same bake-res compensation rule as the spacings above.
    bool           ditherScreen = false;
    DitherSettings ditherSettings;
    // Mosaic screen layer (instanced tile fill, uniform-driven): mosaic.vert
    // rebuilds the lattice like dot.vert and colours each tile from a mip
    // average of the adjust chain's source. `mosaicSettings.spacing` follows
    // the bake-res compensation rule. Text labels force the CPU path.
    bool           mosaicScreen = false;
    MosaicSettings mosaicSettings;
    // ASCII screen layer, Square grid (coverage-ramp fullscreen pass,
    // uniform-driven): ascii.frag maps output pixels to glyph cells and
    // samples a glyph atlas (built CPU-side, cached per font/charset/cell).
    bool          asciiScreen = false;
    // ASCII, non-square lattice (Hex/Brick/Wave/Radial/Phyllotaxis): one
    // glyph billboard per GridGenerator sample, ascii_grid.vert/.frag —
    // same instanced pattern as dotScreen/mosaicScreen. Mutually exclusive
    // with asciiScreen (AsciiRenderer::gpuInstanced picks one).
    bool          asciiInstanced = false;
    // `cellSize` follows the bake-res compensation rule either way.
    // Effects (non-square only)/Braille/Palette → CPU (AsciiRenderer::
    // gpuRenderable).
    AsciiSettings asciiSettings;
    QSize          contentSize;   // raster size for both paths (== image.size() when raster)
};

struct GpuFramePackage {
    QVector<GpuLayer> layers;   // bottom → top paint order
    QSize             frame;
    QColor            bg;       // straight-alpha background colour
    bool              valid = false;
};
