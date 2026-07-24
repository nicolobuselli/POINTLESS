#pragma once

#include <QObject>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QHash>
#include <QMutex>
#include <QFutureWatcher>
#include "../core/Params.h"
#include "../gpu/GpuFramePackage.h"

/**
 * RenderWorker
 *
 * Two-pass rendering strategy:
 *  1. FAST PASS — fires immediately, renders a downscaled version (max 600px)
 *     for instant visual feedback while the user drags sliders.
 *  2. FULL PASS — fires after FULL_DELAY_MS of inactivity, renders at
 *     full resolution.
 *
 * Both passes run on QThreadPool via QtConcurrent::run so the UI never blocks.
 *
 * The static helpers implement the shared document pipeline
 * (adjustments → mode renderer → background) and are reused by export.
 */
class RenderWorker : public QObject
{
    Q_OBJECT

public:
    explicit RenderWorker(QObject* parent = nullptr);
    ~RenderWorker() override;

    // fullPass=false renders only the fast preview (used during playback so
    // the full-resolution pass never floods the thread). layerSrc maps a
    // layerId → the media image that layer draws this frame.
    void requestRender(const QImage& source, const SessionParams& params,
                       bool fullPass = true, const QHash<int, QImage>& layerSrc = {});

    // Full pass only (no fast preview pass): used for the zoom quality re-render,
    // so the on-screen frame isn't replaced by a low-res preview on every scroll.
    void requestFullRender(const QImage& source, const SessionParams& params,
                           const QHash<int, QImage>& layerSrc = {});

    // GPU compositing mode: the fast/full passes stop flattening on the CPU
    // and emit layersComplete(GpuFramePackage) instead — per-layer rendered
    // rasters (same per-layer cache) + placement matrices + blend modes,
    // composited by GpuCanvasWidget. Export/playback paths are unaffected.
    void setGpuPackages(bool on) { m_gpuPackages = on; }

    // Live drag preview resolution, set by the UI to the preview widget's
    // on-screen pixel size — so the fast pass already looks like the final
    // (which is downscaled to the same size), making the swap near-invisible.
    // Upper bound is the app's usual sane working-res ceiling (matches the
    // 6000px caps elsewhere in this file), NOT the old 2000: MainWindow bumps
    // this to the full frame size when every visible layer is GPU-renderable
    // (animCanPlayLive()), since that pass is nearly free on GPU — a 2000 cap
    // used to silently truncate that bump for any frame bigger than 2000px
    // (common: phone photos import at native res), forcing a downscaled —
    // and therefore blurry/wrong-scale — interactive pass even though the
    // full-res one would have been just as cheap.
    void setInteractivePreviewPx(int px) { m_interactivePx = qBound(256, px, 6000); }

    static constexpr int FAST_MAX_PX        = 600;   // preview res for the playback cache
    static constexpr int INTERACTIVE_MAX_PX = 900;   // default live drag preview cap
    static constexpr int FULL_DELAY_MS      = 120;   // ms idle before full render

    // Shared pipeline ------------------------------------------------------
    // Full raster pipeline: background + visible layers (each with its own
    // adjustments) composited with their blend modes.
    static QImage renderDocument(const QImage& source, const SessionParams& params);
    // Per-layer source variant: layerSrc maps layerId → the image that layer
    // draws this frame (falls back to `source` when a layer is absent).
    static QImage renderDocument(const QImage& source, const SessionParams& params,
                                 const QHash<int, QImage>& layerSrc);
    // Render at a downscaled "preview" resolution (matching the fast pass),
    // for building the playback frame cache cheaply.
    static QImage renderPreview(const QImage& source, const SessionParams& params, int maxPx,
                                const QHash<int, QImage>& layerSrc = {});
    // Same as renderPreview, but reuses this instance's layer cache across
    // repeated calls: a playback pre-render loop over many frames only pays
    // for a full renderLayer() when a layer's content-affecting params (or
    // scale) actually changed frame-to-frame — position/rotation/flip-only
    // differences just recomposite the cached raster.
    QImage renderPreviewCached(const QImage& source, const SessionParams& params, int maxPx,
                               const QHash<int, QImage>& layerSrc = {});
    // Full-resolution renderDocument that reuses this instance's layer cache
    // (see renderPreviewCached above) instead of the static renderDocument's
    // fresh render every call. Also usable outside live preview: a sequential
    // export loop (PNG sequence / mp4) that calls this per frame on the same
    // owned RenderWorker instance skips re-rendering any layer whose content-
    // affecting params and source pixels didn't change frame-to-frame (e.g.
    // only position/rotation is animated) — position/rotation always use the
    // live transform regardless of cache hits, so placement stays correct.
    QImage renderDocumentInteractive(const QImage& source, const SessionParams& params,
                                     const QHash<int, QImage>& layerSrc);
    // One layer's renderer, into an open painter (used for SVG export).
    // `adjusted` must already have the layer's adjustments applied.
    static void   renderLayerInto(QPainter& painter, const QImage& adjusted,
                                  const Layer& layer);
    // One layer rendered on a transparent canvas (layer's own adjusted size).
    static QImage renderLayer(const QImage& source, const Layer& layer);

    // Vector export: write the document to an SVG. Halftone/Ascii/Dither layers
    // with a Normal blend are emitted as real shapes (dither cells are merged);
    // other layers (non-Normal blend, Original) are rasterised + embedded so the
    // result still matches. Returns false if the frame size can't be resolved.
    static bool renderDocumentToSvg(const QString& path, const QImage& source,
                                    const SessionParams& params,
                                    const QHash<int, QImage>& layerSrc = {});
    // Rough upper bound on the vector elements an SVG export would contain, for
    // the "heavy render" warning (dots + glyphs + un-merged dither cells).
    static int estimateSvgElements(const QImage& source, const SessionParams& params,
                                   const QHash<int, QImage>& layerSrc = {});

signals:
    void renderComplete(QImage result, bool isPreview);
    void layersComplete(GpuFramePackage pkg, bool isPreview);
    void renderStarted(bool isPreview);

private slots:
    void onFullTimerTimeout();
    void onFastRenderFinished();
    void onFullRenderFinished();
    void onFastPackageFinished();
    void onFullPackageFinished();

private:
    static SessionParams scaledForPreview(const SessionParams& params, float scale);

    // Caches renderLayer()'s output per layer id: position/rotation/flip don't
    // affect that pixel content (only placement does), so a move/rotate-only
    // drag can reuse the last halftone/dither/ascii render and just recomposite.
    // Keyed on (layer id, working-raster size) — the interactive pass (small),
    // the full pass (frame-sized) and a zoomed full pass (supersampled) all use
    // different raster sizes for the SAME layer id and must coexist, or every
    // switch between them (e.g. a drag's fast passes vs. the full-quality pass
    // that follows on release) evicts the other and forces a full re-render.
    struct LayerCacheEntry {
        Layer       key;               // origLayer with xPct/yPct/rotation/flip zeroed
        QSize       srcSize;
        const void* srcBits = nullptr; // identity check only, never dereferenced
        QImage      rendered;
    };
    static QImage renderDocumentImpl(const QImage& source, const SessionParams& params,
                                     const QHash<int, QImage>& layerSrc,
                                     QHash<int, QHash<qint64, LayerCacheEntry>>* cache,
                                     QMutex* cacheMutex);

    QHash<int, QHash<qint64, LayerCacheEntry>> m_layerCache;
    QMutex                                     m_layerCacheMutex;

    QTimer            m_fullTimer;
    QImage            m_sourceImage;
    SessionParams     m_latestParams;
    QHash<int,QImage> m_layerSrc;    // per-layer media for the current frame

    // Reused across consecutive launchFast() calls at the same source + scale
    // (the common case while dragging a layer: only position changes), so the
    // downscaled buffer's address stays stable and the layer-render cache
    // above (keyed in part on that address) can actually hit. Without this,
    // QImage::scaled() allocated a fresh buffer every single interactive frame
    // and the cache never hit during a drag.
    QImage      m_cachedSmallSrc;
    const void* m_cachedSmallSrcOrigBits = nullptr;
    float       m_cachedSmallSrcK        = -1.0f;

    // Same fix for per-layer media (scaledLayerSrc): without it every
    // interactive frame re-scales every layer's media into a fresh buffer,
    // whose new address busts the layer-render cache on every tick.
    struct ScaledSrcEntry { const void* bits = nullptr; float k = -1.0f; QImage img; };
    QHash<int, ScaledSrcEntry> m_scaledLayerSrcCache;
    QHash<int, QImage> scaledLayerSrcCached(const QHash<int, QImage>& src, float scale);

    int   m_interactivePx     = INTERACTIVE_MAX_PX;

    QFutureWatcher<QImage> m_fastWatcher;
    QFutureWatcher<QImage> m_fullWatcher;
    QFutureWatcher<GpuFramePackage> m_fastPkgWatcher;
    QFutureWatcher<GpuFramePackage> m_fullPkgWatcher;
    bool m_fastPending = false;
    bool m_fullPending = false;
    bool m_gpuPackages = false;

    bool fastBusy() const { return m_fastWatcher.isRunning() || m_fastPkgWatcher.isRunning(); }
    bool fullBusy() const { return m_fullWatcher.isRunning() || m_fullPkgWatcher.isRunning(); }

    // Per-layer render (cached) without CPU placement/compositing — the GPU
    // compositor does those. Mirrors renderDocumentImpl's cache semantics.
    GpuFramePackage renderLayersPackage(const QImage& source, const SessionParams& params,
                                        const QHash<int, QImage>& layerSrc);

    void launchFast();
    void launchFull();
};
