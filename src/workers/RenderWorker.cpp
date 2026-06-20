#include "RenderWorker.h"
#include "../core/ImageAdjuster.h"
#include "../core/HalftoneRenderer.h"
#include "../core/DitherRenderer.h"
#include "../core/AsciiRenderer.h"
#include "../core/BlendCompositor.h"

#include <QPainter>
#include <QtConcurrent/QtConcurrent>
#include <cmath>

namespace {

float symbolSupersampleFactor(const Layer& layer, const QSize& size)
{
    if (layer.kind == LayerKind::Dither || layer.kind == LayerKind::Original)
        return 1.0f;

    float target = 1.0f;
    if (layer.kind == LayerKind::Halftone) {
        const int dpi = qBound(18, layer.halftone.inputDpi, 300);
        target = qBound(1.0f, 72.0f / float(dpi), 2.0f);
    } else if (layer.kind == LayerKind::Ascii) {
        const int cell = qBound(4, layer.ascii.cellSize, 128);
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

void RenderWorker::renderLayerInto(QPainter& painter, const QImage& adjusted,
                                   const Layer& layerIn)
{
    // A removed fill (Fill section "−") paints no ink at all: the shapes have
    // no fill, so the layer contributes nothing (stroke, when added, would
    // still paint). The stored palette is left intact for when it's restored.
    const Layer& layer = layerIn;
    switch (layer.kind) {
        case LayerKind::Halftone: if (!layer.halftone.tonal.enabled) return; break;
        case LayerKind::Dither:   if (!layer.dither.tonal.enabled)   return; break;
        case LayerKind::Ascii:    if (!layer.ascii.tonal.enabled)    return; break;
        case LayerKind::Original: break;
    }

    switch (layer.kind) {
        case LayerKind::Original: {
            painter.drawImage(0, 0, adjusted);
            break;
        }
        case LayerKind::Halftone: {
            const HalftoneSettings& hs = layer.halftone;
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
        case LayerKind::Dither: {
            QImage d = DitherRenderer::render(adjusted, layer.dither);
            painter.save();
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(QRect(0, 0, adjusted.width(), adjusted.height()), d);
            painter.restore();
            break;
        }
        case LayerKind::Ascii: {
            AsciiRenderer::render(adjusted, painter, layer.ascii);
            break;
        }
    }
}

QImage RenderWorker::renderLayer(const QImage& source, const Layer& layer)
{
    // Every layer renders from its own embedded reference image.
    const QImage adjusted = ImageAdjuster::apply(source, layer.adjustments);

    if (layer.kind == LayerKind::Original)
        return adjusted.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const QSize outSize = adjusted.size();
    const float ss = symbolSupersampleFactor(layer, outSize);

    if (ss > 1.01f) {
        const int hiW = qMax(1, qRound(outSize.width() * ss));
        const int hiH = qMax(1, qRound(outSize.height() * ss));

        QImage hi(hiW, hiH, QImage::Format_ARGB32_Premultiplied);
        hi.fill(Qt::transparent);

        QPainter hp(&hi);
        hp.setRenderHint(QPainter::Antialiasing, true);
        hp.setRenderHint(QPainter::TextAntialiasing, true);
        hp.scale(qreal(hiW) / outSize.width(), qreal(hiH) / outSize.height());
        renderLayerInto(hp, adjusted, layer);
        hp.end();

        return hi.scaled(outSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QImage img(outSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    renderLayerInto(painter, adjusted, layer);
    painter.end();

    return img;
}

QImage RenderWorker::renderDocument(const QImage& source, const SessionParams& params)
{
    return renderDocument(source, params, {});
}

QImage RenderWorker::renderDocument(const QImage& source, const SessionParams& params,
                                    const QHash<int, QImage>& layerSrc)
{
    // Canvas size: the document base, or (if there's no base) the first
    // visible layer's own media.
    QSize outSize = source.isNull() ? QSize() : source.size();
    if (outSize.isEmpty()) {
        for (const Layer& l : params.layers) {
            const QImage s = layerSrc.value(l.id);
            if (!s.isNull()) { outSize = s.size(); break; }
        }
    }
    if (outSize.isEmpty()) return {};

    QColor bg = params.background;
    bg.setAlphaF(params.backgroundOpacity);

    QImage canvas(outSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(bg);

    // Layer stack is stored top→bottom (UI order); composite bottom→top.
    for (auto it = params.layers.rbegin(); it != params.layers.rend(); ++it) {
        if (!it->visible) continue;
        const QImage src = layerSrc.contains(it->id) ? layerSrc.value(it->id) : source;
        if (src.isNull()) continue;
        QImage layerImg = renderLayer(src, *it);
        if (layerImg.size() != outSize)
            layerImg = layerImg.scaled(outSize, Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
        BlendCompositor::compositeOver(canvas, layerImg, it->blend);
    }

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

void RenderWorker::requestRender(const QImage& source, const SessionParams& params,
                                 bool fullPass, const QHash<int, QImage>& layerSrc)
{
    m_sourceImage  = source;
    m_latestParams = params;
    m_layerSrc     = layerSrc;

    if (m_fastWatcher.isRunning()) {
        m_fastPending = true;
    } else {
        launchFast();
    }

    if (fullPass) m_fullTimer.start();
    else          m_fullTimer.stop();
}

QImage RenderWorker::renderPreview(const QImage& source, const SessionParams& params, int maxPx,
                                   const QHash<int, QImage>& layerSrc)
{
    const int maxDim = qMax(source.width(), source.height());
    if (source.isNull() || maxDim <= maxPx || maxDim <= 0)
        return renderDocument(source, params, layerSrc);

    const QImage small = source.scaled(maxPx, maxPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    SessionParams p = scaledForPreview(params, float(maxPx) / float(maxDim));
    // Keep halftone dot scale consistent with the full render (mirrors launchFast).
    for (Layer& l : p.layers) {
        if (l.kind != LayerKind::Halftone) continue;
        const float requested = qBound(18, l.halftone.inputDpi, 300) / 72.0f;
        const float maxBySize = 6000.0f / float(maxDim);
        const float minBySize = 16.0f   / float(maxDim);
        const float eff = qBound(minBySize, requested, maxBySize);
        l.halftone.inputDpi = qBound(18, qRound(eff * 72.0f), 300);
    }
    return renderDocument(small, p, layerSrc);
}

SessionParams RenderWorker::scaledForPreview(const SessionParams& params, float scale)
{
    SessionParams p = params;
    for (Layer& l : p.layers) {
        l.ascii.cellSize   = qMax(3, int(l.ascii.cellSize * scale));
        l.dither.pixelSize = qMax(1, qRound(l.dither.pixelSize * scale));
        l.adjustments.sharpenRadius = qMax(1, qRound(l.adjustments.sharpenRadius * scale));
    }
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

        // Keep preview and full render at the same effective halftone scale.
        for (Layer& l : previewParams.layers) {
            if (l.kind != LayerKind::Halftone) continue;
            const float requestedScale = qBound(18, l.halftone.inputDpi, 300) / 72.0f;
            const float fullScaleMaxBySize = 6000.0f / float(maxDim);
            const float fullScaleMinBySize = 16.0f / float(maxDim);
            const float effectiveFullScale = qBound(fullScaleMinBySize, requestedScale, fullScaleMaxBySize);
            l.halftone.inputDpi = qBound(18, qRound(effectiveFullScale * 72.0f), 300);
        }
    }

    QImage src = preview;
    SessionParams p = previewParams;

    emit renderStarted(true);

    const QHash<int,QImage> ls = m_layerSrc;
    m_fastWatcher.setFuture(QtConcurrent::run([src, p, ls]() {
        return renderDocument(src, p, ls);
    }));
}

void RenderWorker::launchFull()
{
    if (m_sourceImage.isNull()) return;

    QImage src = m_sourceImage;
    SessionParams p = m_latestParams;
    const QHash<int,QImage> ls = m_layerSrc;

    emit renderStarted(false);

    m_fullWatcher.setFuture(QtConcurrent::run([src, p, ls]() {
        return renderDocument(src, p, ls);
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
