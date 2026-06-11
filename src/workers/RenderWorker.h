#pragma once

#include <QObject>
#include <QImage>
#include <QTimer>
#include <QFutureWatcher>
#include "../core/HalftoneParams.h"

/**
 * RenderWorker
 *
 * Two-pass rendering strategy:
 *  1. FAST PASS — fires immediately, renders a downscaled version (max 512px)
 *     for instant visual feedback while the user drags sliders.
 *  2. FULL PASS — fires after FULL_DELAY_MS of inactivity, renders at
 *     full resolution.
 *
 * Both passes run on QThreadPool via QtConcurrent::run so the UI never blocks.
 */
class RenderWorker : public QObject
{
    Q_OBJECT

public:
    explicit RenderWorker(QObject* parent = nullptr);
    ~RenderWorker() override;

    void requestRender(const QImage& source, const HalftoneParams& params);

    // Tune these if needed
    static constexpr int FAST_MAX_PX   = 600;   // max dimension for preview
    static constexpr int FULL_DELAY_MS = 350;    // ms idle before full render

signals:
    void renderComplete(QImage result, bool isPreview);
    void renderStarted(bool isPreview);

private slots:
    void onFullTimerTimeout();
    void onFastRenderFinished();
    void onFullRenderFinished();

private:
    static QImage doRender(QImage source, HalftoneParams params);

    QTimer         m_fullTimer;
    QImage         m_sourceImage;
    HalftoneParams m_latestParams;

    QFutureWatcher<QImage> m_fastWatcher;
    QFutureWatcher<QImage> m_fullWatcher;
    bool m_fastPending = false;
    bool m_fullPending = false;

    void launchFast();
    void launchFull();
};
