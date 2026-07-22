#pragma once

#include <QRhiWidget>
#include <QImage>
#include <QRectF>
#include <memory>
#include <unordered_map>
#include <functional>
#include "../gpu/GpuFramePackage.h"

class QRhi;
class QRhiBuffer;
class QRhiTexture;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiGraphicsPipeline;
class QRhiTextureRenderTarget;
class QRhiRenderPassDescriptor;
class QRhiCommandBuffer;
class QRhiResourceUpdateBatch;

// ============================================================
//  GpuCanvasWidget — the GPU canvas (Phase 1: GPU compositor).
//
//  Two display modes:
//   · Image   — blit one flattened QImage (playback frames, the
//               "show original" toggle, CPU-fallback content).
//   · Package — per-layer compositing on the GPU: each layer's
//               CPU-rendered raster uploads as a texture (cached
//               by QImage::cacheKey, so unchanged layers upload
//               nothing), then one fullscreen pass per layer
//               applies placement + the Photoshop blend mode
//               (composite.frag mirrors BlendCompositor.cpp),
//               ping-ponging between two offscreen targets.
//
//  The result is blitted into the widget at `viewRect` (logical
//  widget coords) — PreviewWidget drives that rect so the GPU
//  image and the QPainter overlays share identical geometry.
// ============================================================

class GpuCanvasWidget : public QRhiWidget
{
    Q_OBJECT

public:
    explicit GpuCanvasWidget(QWidget* parent = nullptr);
    ~GpuCanvasWidget() override;

    void showImage(const QImage& img);
    void showPackage(const GpuFramePackage& pkg);
    void setViewRect(const QRectF& rectWidgetCoords);

    bool isInitialized() const { return m_initialized; }

    // One step of the Phase-4 adjust chain (public: planned by a free helper).
    struct AdjPass {
        int   op;          // adjust.frag opFlags.x
        int   radius = 0;  // blur/unsharp box radius
        float amount = 0.0f;   // edge/unsharp strength
        int   src = -1;    // ping index; -1 = rawTex
        int   aux = -1;    // ping index for texB (op 4); -1 = dummy
        int   dst = -2;    // ping index; -2 = outTex
    };

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    void ensureAccumTargets(const QSize& size);
    void ensureLayerTextures(QRhiResourceUpdateBatch* rub);
    void ensureDotPipeline();
    void ensureAdjPipelines();
    QRhiTexture* ensureAdjustRes(const GpuLayer& l, bool f16Out, QRhiResourceUpdateBatch* rub);
    void rebuildCompositeSrbs();
    QMatrix4x4 presentMatrix() const;

    QRhi* m_rhi = nullptr;
    bool  m_initialized = false;

    // Shared
    std::unique_ptr<QRhiBuffer>  m_vbuf;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_dummyTex;   // 1×1, layout-only SRBs
    bool m_vbufDirty = true;

    // Present (blit) pass
    std::unique_ptr<QRhiBuffer>                 m_blitUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_blitSrb;
    std::unique_ptr<QRhiGraphicsPipeline>       m_blitPipeline;
    QRhiTexture* m_presentTex = nullptr;   // borrowed: accum or image texture

    // Image mode
    std::unique_ptr<QRhiTexture> m_imageTex;
    QImage m_image;
    bool   m_imageDirty = false;

    // Package mode
    GpuFramePackage m_pkg;
    // One small UBO per layer pass (all updated in one batch up front, so no
    // mid-frame dynamic-buffer aliasing questions).
    std::vector<std::unique_ptr<QRhiBuffer>> m_compUbufs;
    std::unique_ptr<QRhiGraphicsPipeline>    m_compPipeline;
    std::unique_ptr<QRhiTexture>             m_accum[2];
    std::unique_ptr<QRhiTextureRenderTarget> m_accumRt[2];
    std::unique_ptr<QRhiRenderPassDescriptor> m_accumRp;
    QSize m_accumSize;

    struct LayerTex {
        qint64 key = -1;
        std::unique_ptr<QRhiTexture> tex;
    };
    // Keyed by (layer id, raster size) — the fast (≤900px) and full-res
    // rasters of the same layer keep SEPARATE textures, so the fast↔full
    // alternation around every drag pause stops destroying/recreating/
    // re-uploading textures (that churn was a visible hitch).
    std::unordered_map<quint64, LayerTex> m_layerTex;   // move-only values: not QHash
    std::vector<QRhiTexture*> m_frameTex;   // per-pkg-layer src textures, this frame

    // Dot Grid screen layers (fully uniform-driven): dot.vert reconstructs
    // every sample position from gl_InstanceIndex and reads cell tone/colour
    // from the linear fp16 mipmapped source, so ANY control drag — spacing
    // and grid layout included — is just a UBO update. Per layer: source
    // texture, UBO, and the offscreen content target the dots render into;
    // the content texture then enters the composite chain like any raster.
    struct DotRes {
        QRhiTexture* srcBound = nullptr;       // adjust-chain output the SRB binds
        std::unique_ptr<QRhiBuffer>  ubo;      // mvp + dot/grid params
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        std::unique_ptr<QRhiTexture> tex;      // content target
        std::unique_ptr<QRhiTextureRenderTarget>    rt;
        QSize size;
        int   dotCount = 0;                    // instances to draw (GridGpuLayout)
    };
    std::unordered_map<int, DotRes> m_dotRes;
    std::unique_ptr<QRhiGraphicsPipeline>     m_dotPipeline;
    std::unique_ptr<QRhiRenderPassDescriptor> m_dotRp;

    // Halftone screen layers: fully uniform-driven fullscreen pass — the
    // linear fp16 source (with mips, from the adjust chain) is sampled by
    // halftone.frag into the content target; every parameter lives in the UBO.
    struct HalfRes {
        QRhiTexture* srcBound = nullptr;       // adjust-chain output the SRB binds
        std::unique_ptr<QRhiBuffer>  ubo;      // 528B mvp + params
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        std::unique_ptr<QRhiTexture> tex;      // content target
        std::unique_ptr<QRhiTextureRenderTarget>    rt;
        QSize size;                            // content size
    };
    std::unordered_map<int, HalfRes> m_halfRes;
    std::unique_ptr<QRhiGraphicsPipeline> m_halfPipeline;
    std::unique_ptr<QRhiSampler>          m_mipSampler;   // linear + mip linear
    void ensureHalfPipeline();

    // Dither screen layers (ordered/threshold, uniform-driven): fullscreen
    // pass sampling the adjust chain's linear source per chunky cell;
    // threshold matrix in a small tiled R32F texture (texelFetch).
    struct DitherRes {
        QRhiTexture* srcBound = nullptr;       // adjust-chain output the SRB binds
        std::unique_ptr<QRhiTexture> maskTex;  // R32F threshold matrix
        quint64 maskKey = 0;                   // (algorithm, bayerSize, patternPath)
        std::unique_ptr<QRhiBuffer>  ubo;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        std::unique_ptr<QRhiTexture> tex;      // content target
        std::unique_ptr<QRhiTextureRenderTarget>    rt;
        QSize size;
    };
    std::unordered_map<int, DitherRes> m_ditherRes;
    std::unique_ptr<QRhiGraphicsPipeline> m_ditherPipeline;
    void ensureDitherPipeline();

    // Mosaic screen layers: same shape as DotRes (instanced tiles, source
    // sampled in the vertex stage), own UBO layout + pipeline.
    std::unordered_map<int, DotRes> m_mosRes;
    std::unique_ptr<QRhiGraphicsPipeline> m_mosPipeline;
    void ensureMosPipeline();

    // ASCII screen layers (coverage-ramp, uniform-driven): fullscreen pass
    // sampling the adjust chain's source per cell + a glyph atlas texture
    // (built CPU-side by AsciiRenderer::gpuAtlas, keyed by font/charset/cell).
    struct AsciiRes {
        QRhiTexture* srcBound = nullptr;       // adjust-chain output the SRB binds
        std::unique_ptr<QRhiTexture> atlasTex; // RGBA8 glyph atlas
        quint64 atlasKey = 0;
        std::unique_ptr<QRhiBuffer>  ubo;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        std::unique_ptr<QRhiTexture> tex;      // content target
        std::unique_ptr<QRhiTextureRenderTarget>    rt;
        QSize size;
    };
    std::unordered_map<int, AsciiRes> m_asciiRes;
    std::unique_ptr<QRhiGraphicsPipeline> m_asciiPipeline;
    void ensureAsciiPipeline();

    // ASCII, non-square lattice: instanced glyph billboards (ascii_grid.
    // vert/.frag) — same pattern as DotRes but with an extra atlas texture
    // bound in the fragment stage (DotRes only binds srcTex, vertex-only).
    struct AsciiGridRes {
        QRhiTexture* srcBound = nullptr;
        std::unique_ptr<QRhiTexture> atlasTex;
        quint64 atlasKey = 0;
        std::unique_ptr<QRhiBuffer>  ubo;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        std::unique_ptr<QRhiTexture> tex;
        std::unique_ptr<QRhiTextureRenderTarget>    rt;
        QSize size;
        int   count = 0;
    };
    std::unordered_map<int, AsciiGridRes> m_asciiGridRes;
    std::unique_ptr<QRhiGraphicsPipeline> m_asciiGridPipeline;
    void ensureAsciiGridPipeline();

    // Phase 4 — GPU adjust chain (adjust.vert/.frag): per layer, the RAW
    // straight-alpha source runs the full ImageAdjuster pipeline as a short
    // sequence of fullscreen passes (plan in `plan`), ping-ponging fp16
    // targets into `outTex`: linear fp16 + mips for screen layers (dot/
    // halftone sample it), premultiplied RGBA8 for Original layers with
    // neighborhood ops (composite samples it as a plain raster). Any
    // adjustment slider = UBO rewrite + re-run of the (sub-ms) chain.
    struct AdjRes {
        std::unique_ptr<QRhiTexture> rawTex;   // RGBA8 mipmapped, straight alpha
        qint64 rawKey = -1;
        std::unique_ptr<QRhiTexture>             ping[3];    // RGBA16F work targets
        std::unique_ptr<QRhiTextureRenderTarget> pingRt[3];
        std::unique_ptr<QRhiTexture>             outTex;
        std::unique_ptr<QRhiTextureRenderTarget> outRt;
        QSize srcSize, outSize;
        bool  f16Out = false;
        std::vector<std::unique_ptr<QRhiBuffer>>                 ubos;   // per pass
        std::vector<std::unique_ptr<QRhiShaderResourceBindings>> srbs;   // per pass
        std::vector<int> planSig;              // (op,src,aux,dst)* the SRBs match
        std::vector<AdjPass> plan;             // passes to run this frame
    };
    std::unordered_map<int, AdjRes> m_adjRes;
    std::unique_ptr<QRhiGraphicsPipeline>     m_adjPipeF16, m_adjPipe8;
    std::unique_ptr<QRhiRenderPassDescriptor> m_adjRpF16,   m_adjRp8;
    std::vector<std::unique_ptr<QRhiShaderResourceBindings>> m_compSrbs;
    std::vector<void*> m_srbSig;            // {accum0, accum1, srcTex...} the SRBs were built for
    QRhiTexture* m_blitSrbTex = nullptr;    // texture the blit SRB was built for

    bool   m_packageMode = false;
    // render() used to redo every content + compositing pass on EVERY repaint
    // — including a pure resize (splitter/panel drag) or a pan/zoom, which
    // only need the cheap final blit re-done. Set whenever showPackage() gets
    // fresh data; render() only re-runs the heavy passes while this is true,
    // otherwise it reuses the previous composite (m_presentTex) as-is.
    bool   m_pkgDirty = true;
    QRectF m_viewRect;
};
