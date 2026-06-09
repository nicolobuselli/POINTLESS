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
    canvas.fill(Qt::white);

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
}

RenderWorker::~RenderWorker() = default;

void RenderWorker::requestRender(const QImage& source, const HalftoneParams& params)
{
    m_sourceImage  = source;
    m_latestParams = params;

    // 1. Launch fast preview immediately
    launchFast();

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
                                 Qt::FastTransformation);
    }

    // Adjust grid size proportionally so halftone density looks the same
    HalftoneParams previewParams = m_latestParams;
    if (maxDim > FAST_MAX_PX) {
        float scale = float(FAST_MAX_PX) / float(maxDim);
        previewParams.gridSize = qMax(2, int(m_latestParams.gridSize * scale));
    }

    int gen = ++m_fastGeneration;
    QImage src    = preview;
    HalftoneParams p = previewParams;

    emit renderStarted(true);

    QtConcurrent::run([this, src, p, gen]() {
        QImage result = doRender(src, p);
        // Scale back up to original size so preview widget doesn't jump
        if (src.size() != m_sourceImage.size()) {
            result = result.scaled(m_sourceImage.size(),
                                   Qt::IgnoreAspectRatio,
                                   Qt::FastTransformation);
        }
        // Only emit if still the latest fast request
        if (gen == m_fastGeneration) {
            emit renderComplete(result, true);
        }
    });
}

void RenderWorker::launchFull()
{
    if (m_sourceImage.isNull()) return;

    int gen = ++m_fullGeneration;
    QImage src = m_sourceImage;
    HalftoneParams p = m_latestParams;

    emit renderStarted(false);

    QtConcurrent::run([this, src, p, gen]() {
        QImage result = doRender(src, p);
        if (gen == m_fullGeneration) {
            emit renderComplete(result, false);
        }
    });
}

void RenderWorker::onFullTimerTimeout()
{
    launchFull();
}
