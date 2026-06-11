#include "RenderWorker.h"
#include "../core/HalftoneRenderer.h"

#include <QPainter>
#include <QtConcurrent/QtConcurrent>

// ---------------------------------------------------------------------------
// Static render function (runs on thread pool)
// ---------------------------------------------------------------------------

QImage RenderWorker::doRender(QImage source, HalftoneParams params)
{
    QImage canvas(source.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    HalftoneRenderer renderer;
    renderer.render(source, painter, params);
    painter.end();

    return canvas;
}

// ---------------------------------------------------------------------------
// RenderWorker
// ---------------------------------------------------------------------------

RenderWorker::RenderWorker(QObject* parent)
    : QObject(parent)
{
    m_fullTimer.setSingleShot(true);
    m_fullTimer.setInterval(FULL_DELAY_MS);
    connect(&m_fullTimer, &QTimer::timeout, this, &RenderWorker::onFullTimerTimeout);
    connect(&m_fastWatcher, &QFutureWatcher<QImage>::finished,
            this, &RenderWorker::onFastRenderFinished);
    connect(&m_fullWatcher, &QFutureWatcher<QImage>::finished,
            this, &RenderWorker::onFullRenderFinished);
}

RenderWorker::~RenderWorker() = default;

void RenderWorker::requestRender(const QImage& source, const HalftoneParams& params)
{
    m_sourceImage  = source;
    m_latestParams = params;

    // 1. Launch fast preview immediately (or queue one pending refresh)
    if (m_fastWatcher.isRunning()) {
        m_fastPending = true;
    } else {
        launchFast();
    }

    // 2. Restart full-res timer — fires when user stops interacting
    m_fullTimer.start();
}

void RenderWorker::launchFast()
{
    if (m_sourceImage.isNull()) return;

    // Downscale source for fast preview
    QImage preview = m_sourceImage;
    int maxDim = qMax(preview.width(), preview.height());
    if (maxDim > FAST_MAX_PX) {
        preview = preview.scaled(FAST_MAX_PX, FAST_MAX_PX,
                                 Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
    }

    // Adjust grid size proportionally so halftone density looks the same
    HalftoneParams previewParams = m_latestParams;
    if (maxDim > FAST_MAX_PX) {
        float scale = float(FAST_MAX_PX) / float(maxDim);
        previewParams.gridSize = qMax(2, int(m_latestParams.gridSize * scale));
    }

    QImage src = preview;
    HalftoneParams p = previewParams;

    emit renderStarted(true);

    m_fastWatcher.setFuture(QtConcurrent::run([src, p]() {
        return doRender(src, p);
    }));
}

void RenderWorker::launchFull()
{
    if (m_sourceImage.isNull()) return;

    QImage src = m_sourceImage;
    HalftoneParams p = m_latestParams;

    emit renderStarted(false);

    m_fullWatcher.setFuture(QtConcurrent::run([src, p]() {
        return doRender(src, p);
    }));
}

void RenderWorker::onFullTimerTimeout()
{
    if (m_fullWatcher.isRunning()) {
        m_fullPending = true;
        return;
    }
    launchFull();
}

void RenderWorker::onFastRenderFinished()
{
    QImage result = m_fastWatcher.result();
    emit renderComplete(result, true);

    if (m_fastPending) {
        m_fastPending = false;
        launchFast();
    }
}

void RenderWorker::onFullRenderFinished()
{
    QImage result = m_fullWatcher.result();
    emit renderComplete(result, false);

    if (m_fullPending) {
        m_fullPending = false;
        launchFull();
    }
}
