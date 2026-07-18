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
    // no texture re-upload. Neighborhood ops (blur/sharpen/edge) force the
    // CPU-adjusted path, gpuAdjust=false.
    Adjustments adj;
    bool        gpuAdjust = false;

    // Dot Grid screen layer (fully uniform-driven, like halftone): `image`
    // holds the ADJUSTED SOURCE in linear-light RGBA16F; dot.vert derives
    // every sample position from gl_InstanceIndex (GridGenerator layout) and
    // the cell tone/colour from a textureLod on the mipmapped source, so ANY
    // Dot Grid control — spacing and grid layout included — is a UBO update.
    bool            dotScreen = false;
    // Effective settings for the UBO: the live layer's DotGridSettings with
    // grid.spacing already multiplied by the bake's compensation factor
    // (compensateSymbolScale + prerenderAtFrameRes), i.e. in contentSize px.
    DotGridSettings dotSettings;
    // Halftone screen layer (fully uniform-driven): `image` holds the
    // ADJUSTED SOURCE converted to linear-light RGBA16F (Format_RGBA16FPx4);
    // GpuCanvasWidget uploads it with mips and halftone.frag renders the AM
    // screens per-pixel into the content texture. `halftoneSettings` follows
    // the same spacing-compensation rule as dotSettings.
    bool             halftoneScreen = false;
    HalftoneSettings halftoneSettings;
    QSize          contentSize;   // raster size for both paths (== image.size() when raster)
};

struct GpuFramePackage {
    QVector<GpuLayer> layers;   // bottom → top paint order
    QSize             frame;
    QColor            bg;       // straight-alpha background colour
    bool              valid = false;
};
