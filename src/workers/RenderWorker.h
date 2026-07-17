#pragma once

#include <QObject>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QHash>
#include <QMutex>
#include <QFutureWatcher>
#include "../core/Params.h"

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
    // cheapDrag=true is for a live canvas drag (position/rotation/scale):
    // symbol pitch (halftone spacing, ascii cell, dither pixel) is left at
    // its authored size instead of shrinking with the preview, so symbol
    // COUNT drops with the preview area instead of staying constant — the
    // fast pass actually gets cheaper. Fine for a transient drag frame;
    // the full-quality pass on release restores authored pitch.
    void requestRender(const QImage& source, const SessionParams& params,
                       bool fullPass = true, const QHash<int, QImage>& layerSrc = {},
                       bool cheapDrag = false);

    // Full pass only (no fast preview pass): used for the zoom quality re-render,
    // so the on-screen frame isn't replaced by a low-res preview on every scroll.
    void requestFullRender(const QImage& source, const SessionParams& params,
                           const QHash<int, QImage>& layerSrc = {});

    // Live drag preview resolution, set by the UI to the preview widget's
    // on-screen pixel size — so the fast pass already looks like the final
    // (which is downscaled to the same size), making the swap near-invisible.
    void setInteractivePreviewPx(int px) { m_interactivePx = qBound(256, px, 2000); }

    // Supersample the full pass when the user has zoomed in, so the (vector)
    // symbols are rendered at the displayed resolution instead of upscaling a
    // frame-sized raster. 1.0 = render at frame resolution. Capped in launchFull.
    void setFullQualityScale(float s) { m_fullQualityScale = qBound(1.0f, s, 6.0f); }
    static constexpr int FULL_QUALITY_MAX_PX = 4096;   // budget cap for the full pass

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
    void renderStarted(bool isPreview);

private slots:
    void onFullTimerTimeout();
    void onFastRenderFinished();
    void onFullRenderFinished();

private:
    // shrinkSymbols=false skips the symbol-pitch-follows-image-scale step
    // (see requestRender's cheapDrag) — used only for the interactive pass
    // during a canvas drag.
    static SessionParams scaledForPreview(const SessionParams& params, float scale,
                                          bool shrinkSymbols = true);

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
    QImage renderDocumentInteractive(const QImage& source, const SessionParams& params,
                                     const QHash<int, QImage>& layerSrc);

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

    int   m_interactivePx     = INTERACTIVE_MAX_PX;
    float m_fullQualityScale  = 1.0f;   // zoom-driven supersample for the full pass
    bool  m_cheapDrag         = false;  // true while requestRender's cheapDrag is set

    QFutureWatcher<QImage> m_fastWatcher;
    QFutureWatcher<QImage> m_fullWatcher;
    bool m_fastPending = false;
    bool m_fullPending = false;

    void launchFast();
    void launchFull();
};
