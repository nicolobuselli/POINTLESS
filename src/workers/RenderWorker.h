#pragma once

#include <QObject>
#include <QImage>
#include <QPainter>
#include <QTimer>
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

    void requestRender(const QImage& source, const SessionParams& params);

    static constexpr int FAST_MAX_PX   = 600;   // max dimension for preview
    static constexpr int FULL_DELAY_MS = 350;   // ms idle before full render

    // Shared pipeline ------------------------------------------------------
    // Full raster pipeline: background + visible layers (each with its own
    // adjustments) composited with their blend modes.
    static QImage renderDocument(const QImage& source, const SessionParams& params);
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

    QTimer        m_fullTimer;
    QImage        m_sourceImage;
    SessionParams m_latestParams;

    QFutureWatcher<QImage> m_fastWatcher;
    QFutureWatcher<QImage> m_fullWatcher;
    bool m_fastPending = false;
    bool m_fullPending = false;

    void launchFast();
    void launchFull();
};
