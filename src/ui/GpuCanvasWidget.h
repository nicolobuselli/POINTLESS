#pragma once

#include <QRhiWidget>
#include <QImage>
#include <QRectF>
#include <memory>
#include <unordered_map>
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

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    void ensureAccumTargets(const QSize& size);
    void ensureLayerTextures(QRhiResourceUpdateBatch* rub);
    void ensureDotPipeline();
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
        std::unique_ptr<QRhiTexture> srcTex;   // RGBA16F, mipmapped
        qint64 srcKey = -1;                    // QImage::cacheKey of the upload
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
    // linear fp16 source (with mips) is sampled by halftone.frag into the
    // content target; every parameter lives in the UBO.
    struct HalfRes {
        std::unique_ptr<QRhiTexture> srcTex;   // RGBA16F, mipmapped
        qint64 srcKey  = -1;                   // QImage::cacheKey of the upload
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
    std::vector<std::unique_ptr<QRhiShaderResourceBindings>> m_compSrbs;
    std::vector<void*> m_srbSig;            // {accum0, accum1, srcTex...} the SRBs were built for
    QRhiTexture* m_blitSrbTex = nullptr;    // texture the blit SRB was built for

    bool   m_packageMode = false;
    QRectF m_viewRect;
};
