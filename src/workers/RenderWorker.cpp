#include "RenderWorker.h"
#include "../core/ImageAdjuster.h"
#include "../core/HalftoneRenderer.h"
#include "../core/DitherRenderer.h"
#include "../core/AsciiRenderer.h"

#include <QPainter>
#include <QtConcurrent/QtConcurrent>
#include <cmath>

namespace {

float symbolSupersampleFactor(const SessionParams& params, const QSize& size)
{
    if (params.mode == RenderMode::Dither) return 1.0f;

    float target = 1.0f;
    if (params.mode == RenderMode::Halftone) {
        const int dpi = qBound(18, params.halftone.inputDpi, 300);
        target = qBound(1.0f, 72.0f / float(dpi), 2.0f);
    } else if (params.mode == RenderMode::Ascii) {
        const int cell = qBound(4, params.ascii.cellSize, 128);
        target = qBound(1.0f, 20.0f / float(cell), 1.6f);
    }

    const double srcPixels = double(size.width()) * double(size.height());
    if (srcPixels <= 1.0) return 1.0f;

    // Cap supersampled surface size so low-DPI quality boost stays responsive.
    const double maxPixels = 12.0 * 1000.0 * 1000.0;
    const double maxFactor = std::sqrt(maxPixels / srcPixels);
    return float(qMax(1.0, qMin(double(target), maxFactor)));
}

} // namespace

// ---------------------------------------------------------------------------
// Shared pipeline (runs on thread pool, also used by export)
// ---------------------------------------------------------------------------

void RenderWorker::renderModeInto(QPainter& painter, const QImage& adjusted,
                                  const SessionParams& params)
{
    switch (params.mode) {
        case RenderMode::Halftone: {
            const HalftoneSettings& hs = params.halftone;
            float scale = qBound(18, hs.inputDpi, 300) / 72.0f;

            // Clamp the working resolution to something sane.
            const int maxDim = qMax(adjusted.width(), adjusted.height());
            if (maxDim * scale > 6000.0f) scale = 6000.0f / maxDim;
            if (maxDim * scale < 16.0f)   scale = 16.0f / maxDim;

            if (qAbs(scale - 1.0f) < 0.02f) {
                HalftoneRenderer renderer;
                renderer.render(adjusted, painter, hs);
            } else {
                QImage work = adjusted.scaled(qMax(8, qRound(adjusted.width()  * scale)),
                                              qMax(8, qRound(adjusted.height() * scale)),
                                              Qt::IgnoreAspectRatio,
                                              Qt::SmoothTransformation);
                painter.save();
                painter.scale(qreal(adjusted.width())  / work.width(),
                              qreal(adjusted.height()) / work.height());
                HalftoneRenderer renderer;
                renderer.render(work, painter, hs);
                painter.restore();
            }
            break;
        }
        case RenderMode::Dither: {
            QImage d = DitherRenderer::render(adjusted, params.dither);
            painter.save();
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(QRect(0, 0, adjusted.width(), adjusted.height()), d);
            painter.restore();
            break;
        }
        case RenderMode::Ascii: {
            AsciiRenderer::render(adjusted, painter, params.ascii);
            break;
        }
    }
}

QImage RenderWorker::renderDocument(const QImage& source, const SessionParams& params)
{
    if (source.isNull()) return {};

    const QImage adjusted = ImageAdjuster::apply(source, params.adjustments);
    const QSize outSize = adjusted.size();
    const float ss = symbolSupersampleFactor(params, outSize);

    QColor bg = params.background;
    bg.setAlphaF(params.backgroundOpacity);

    if (ss > 1.01f) {
        const int hiW = qMax(1, qRound(outSize.width() * ss));
        const int hiH = qMax(1, qRound(outSize.height() * ss));

        QImage hi(hiW, hiH, QImage::Format_ARGB32_Premultiplied);
        hi.fill(bg);

        QPainter hp(&hi);
        hp.setRenderHint(QPainter::Antialiasing, true);
        hp.setRenderHint(QPainter::TextAntialiasing, true);
        hp.scale(qreal(hiW) / outSize.width(), qreal(hiH) / outSize.height());
        renderModeInto(hp, adjusted, params);
        hp.end();

        return hi.scaled(outSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QImage canvas(outSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(bg);

    QPainter painter(&canvas);
    renderModeInto(painter, adjusted, params);
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

void RenderWorker::requestRender(const QImage& source, const SessionParams& params)
{
    m_sourceImage  = source;
    m_latestParams = params;

    if (m_fastWatcher.isRunning()) {
        m_fastPending = true;
    } else {
        launchFast();
    }

    m_fullTimer.start();
}

SessionParams RenderWorker::scaledForPreview(const SessionParams& params, float scale)
{
    SessionParams p = params;
    p.ascii.cellSize    = qMax(3, int(params.ascii.cellSize * scale));
    p.dither.pixelSize  = qMax(1, qRound(params.dither.pixelSize * scale));
    p.adjustments.sharpenRadius = qMax(1, qRound(params.adjustments.sharpenRadius * scale));
    return p;
}

void RenderWorker::launchFast()
{
    if (m_sourceImage.isNull()) return;

    QImage preview = m_sourceImage;
    SessionParams previewParams = m_latestParams;

    const int maxDim = qMax(preview.width(), preview.height());
    if (maxDim > FAST_MAX_PX) {
        preview = preview.scaled(FAST_MAX_PX, FAST_MAX_PX,
                                 Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        const float downscale = float(FAST_MAX_PX) / float(maxDim);
        previewParams = scaledForPreview(m_latestParams, downscale);

        if (previewParams.mode == RenderMode::Halftone) {
            // Keep preview and full render at the same effective halftone scale.
            const float requestedScale = qBound(18, previewParams.halftone.inputDpi, 300) / 72.0f;
            const float fullScaleMaxBySize = 6000.0f / float(maxDim);
            const float fullScaleMinBySize = 16.0f / float(maxDim);
            const float effectiveFullScale = qBound(fullScaleMinBySize, requestedScale, fullScaleMaxBySize);
            previewParams.halftone.inputDpi = qBound(18, qRound(effectiveFullScale * 72.0f), 300);
        }
    }

    QImage src = preview;
    SessionParams p = previewParams;

    emit renderStarted(true);

    m_fastWatcher.setFuture(QtConcurrent::run([src, p]() {
        return renderDocument(src, p);
    }));
}

void RenderWorker::launchFull()
{
    if (m_sourceImage.isNull()) return;

    QImage src = m_sourceImage;
    SessionParams p = m_latestParams;

    emit renderStarted(false);

    m_fullWatcher.setFuture(QtConcurrent::run([src, p]() {
        return renderDocument(src, p);
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
