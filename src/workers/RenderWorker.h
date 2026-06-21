#pragma once

#include <QObject>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QHash>
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
    void requestRender(const QImage& source, const SessionParams& params,
                       bool fullPass = true, const QHash<int, QImage>& layerSrc = {});

    // Live drag preview resolution, set by the UI to the preview widget's
    // on-screen pixel size — so the fast pass already looks like the final
    // (which is downscaled to the same size), making the swap near-invisible.
    void setInteractivePreviewPx(int px) { m_interactivePx = qBound(256, px, 2000); }

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
    // One layer's renderer, into an open painter (used for SVG export).
    // `adjusted` must already have the layer's adjustments applied.
    static void   renderLayerInto(QPainter& painter, const QImage& adjusted,
                                  const Layer& layer);
    // One layer rendered on a transparent canvas (layer's own adjusted size).
    static QImage renderLayer(const QImage& source, const Layer& layer);

signals:
    void renderComplete(QImage result, bool isPreview);
    void renderStarted(bool isPreview);

private slots:
    void onFullTimerTimeout();
    void onFastRenderFinished();
    void onFullRenderFinished();

private:
    static SessionParams scaledForPreview(const SessionParams& params, float scale);

    QTimer            m_fullTimer;
    QImage            m_sourceImage;
    SessionParams     m_latestParams;
    QHash<int,QImage> m_layerSrc;    // per-layer media for the current frame

    int m_interactivePx = INTERACTIVE_MAX_PX;

    QFutureWatcher<QImage> m_fastWatcher;
    QFutureWatcher<QImage> m_fullWatcher;
    bool m_fastPending = false;
    bool m_fullPending = false;

    void launchFast();
    void launchFull();
};
