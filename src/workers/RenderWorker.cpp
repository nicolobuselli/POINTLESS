#include "RenderWorker.h"
#include "../core/ImageAdjuster.h"
#include "../core/HalftoneRenderer.h"
#include "../core/DitherRenderer.h"
#include "../core/AsciiRenderer.h"
#include "../core/BlendCompositor.h"

#include <QPainter>
#include <QSvgGenerator>
#include <QtConcurrent/QtConcurrent>
#include <climits>
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
                // DPI is a sampling-resolution control, not a symbol-size one:
                // spacing is authored in output px, so it scales with the work
                // raster and the painter scale-back cancels it exactly.
                HalftoneSettings hw = hs;
                const float kx = float(work.width()) / adjusted.width();
                hw.grid.spacing      *= kx;
                hw.grid.pointSpacing *= kx;
                HalftoneRenderer renderer;
                renderer.render(work, painter, hw);
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

// Transparency = "no image" = paper. Flatten onto white so the tone renderers
// read empty areas as the lightest tone (no ink) — exactly like a white source
// region — instead of solid black. Each mode then treats transparent the way it
// treats white: halftone/ascii draw nothing there, dither lays down paper.
static QImage flattenOntoWhite(const QImage& src)
{
    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::white);
    QPainter p(&out);
    p.drawImage(0, 0, src);
    p.end();
    return out;
}

QImage RenderWorker::renderLayer(const QImage& source, const Layer& layer)
{
    // Every layer renders from its own embedded reference image.
    const QImage adjusted = ImageAdjuster::apply(source, layer.adjustments);

    if (layer.kind == LayerKind::Original)
        return adjusted.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const QImage forRender = flattenOntoWhite(adjusted);
    const QSize outSize = forRender.size();
    const float ss = symbolSupersampleFactor(layer, outSize);

    QImage out;
    if (ss > 1.01f) {
        const int hiW = qMax(1, qRound(outSize.width() * ss));
        const int hiH = qMax(1, qRound(outSize.height() * ss));

        QImage hi(hiW, hiH, QImage::Format_ARGB32_Premultiplied);
        hi.fill(Qt::transparent);

        QPainter hp(&hi);
        hp.setRenderHint(QPainter::Antialiasing, true);
        hp.setRenderHint(QPainter::TextAntialiasing, true);
        hp.scale(qreal(hiW) / outSize.width(), qreal(hiH) / outSize.height());
        renderLayerInto(hp, forRender, layer);
        hp.end();

        out = hi.scaled(outSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    } else {
        out = QImage(outSize, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);

        QPainter painter(&out);
        renderLayerInto(painter, forRender, layer);
        painter.end();
    }

    return out;
}

QImage RenderWorker::renderDocument(const QImage& source, const SessionParams& params)
{
    return renderDocument(source, params, {});
}

// Place a layer's rendered image onto a frame-sized transparent canvas using
// its transform (centre offset, scale, rotation). 100% scale = native pixels.
// When a layer is scaled UP into the frame, its tone symbols would otherwise be
// rasterized at the (small) source resolution and then bitmap-upscaled, looking
// soft — which reads as "low quality source". Render such layers at their
// on-frame resolution instead: enlarge the source and the pixel-pitch params by
// the same factor, then drop the placement scale to match. Identical look, crisp
// symbols. (Mirror of scaledForPreview, which scales the same params down.)
// The scale-only half of prerenderAtFrameRes's math: how much placement scale
// remains after the source is enlarged to frame resolution. No pixel work, so
// it's cheap enough to recompute every frame for cache-hit placement (the
// enlarge factor `rs` depends only on scalePct + source size, not on kind-
// specific settings, so it's stable across the symbol-pitch mutations below).
static float effectivePlacementScalePct(LayerKind kind, float scalePct, QSize srcSize)
{
    if (kind == LayerKind::Original || srcSize.isEmpty()) return scalePct;
    double rs = double(scalePct) / 100.0;
    if (rs <= 1.01) return scalePct;
    const int maxDim = qMax(srcSize.width(), srcSize.height());
    if (maxDim * rs > 6000.0) rs = 6000.0 / maxDim;
    if (rs <= 1.01) return scalePct;
    return float(scalePct / rs);
}

static void prerenderAtFrameRes(QImage& src, Layer& layer)
{
    if (layer.kind == LayerKind::Original || src.isNull()) return;
    const float newScale = effectivePlacementScalePct(layer.kind, layer.transform.scalePct, src.size());
    if (newScale >= layer.transform.scalePct) return;   // not enlarged → nothing to gain
    const double rs = double(layer.transform.scalePct) / double(newScale);

    src = src.scaled(qMax(1, qRound(src.width()  * rs)),
                     qMax(1, qRound(src.height() * rs)),
                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    layer.halftone.grid.spacing = qMax(2.0f, layer.halftone.grid.spacing * float(rs));
    layer.ascii.cellSize        = qMax(3,    int(layer.ascii.cellSize    * rs));
    layer.dither.pixelSize      = qMax(1,    qRound(layer.dither.pixelSize * rs));
    layer.transform.scalePct    = newScale;
}

// Symbol sizes (halftone spacing, ascii cell, dither pixel) are authored in
// FRAME pixels: cancel the layer's placement scale so scaling a layer scales
// the photo, not the symbols. prerenderAtFrameRes then re-applies any upscale
// to both the raster and these params, so the two compose back exactly.
static void compensateSymbolScale(Layer& l)
{
    if (l.kind == LayerKind::Original) return;
    const double s = qMax(0.0001, double(l.transform.scalePct) / 100.0);
    if (qAbs(s - 1.0) < 0.001) return;
    l.halftone.grid.spacing      = float(l.halftone.grid.spacing / s);
    l.halftone.grid.pointSpacing = float(l.halftone.grid.pointSpacing / s);
    // ponytail: int params round here — at extreme scales cell/pixel sizes
    // drift by up to half a source px; make them float if it ever shows.
    l.ascii.cellSize   = qMax(1, qRound(l.ascii.cellSize / s));
    l.dither.pixelSize = qMax(1, qRound(l.dither.pixelSize / s));
}

static QImage placeOnFrame(const QImage& layerImg, const LayerTransform& tf, QSize frame)
{
    QImage placed(frame, QImage::Format_ARGB32_Premultiplied);
    placed.fill(Qt::transparent);
    if (layerImg.isNull()) return placed;

    QPainter p(&placed);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double s  = qMax(0.0001, double(tf.scalePct) / 100.0);
    const double cx = frame.width()  * 0.5 + double(tf.xPct) * frame.width();
    const double cy = frame.height() * 0.5 + double(tf.yPct) * frame.height();

    QTransform m;
    m.translate(cx, cy);
    m.rotate(tf.rotation);
    m.scale(s * (tf.flipH ? -1.0 : 1.0), s * (tf.flipV ? -1.0 : 1.0));
    m.translate(-layerImg.width() * 0.5, -layerImg.height() * 0.5);
    p.setTransform(m);
    p.drawImage(0, 0, layerImg);
    p.end();
    return placed;
}

QImage RenderWorker::renderDocument(const QImage& source, const SessionParams& params,
                                    const QHash<int, QImage>& layerSrc)
{
    return renderDocumentImpl(source, params, layerSrc, nullptr, nullptr);
}

QImage RenderWorker::renderDocumentInteractive(const QImage& source, const SessionParams& params,
                                               const QHash<int, QImage>& layerSrc)
{
    return renderDocumentImpl(source, params, layerSrc, &m_layerCache, &m_layerCacheMutex);
}

QImage RenderWorker::renderDocumentImpl(const QImage& source, const SessionParams& params,
                                        const QHash<int, QImage>& layerSrc,
                                        QHash<int, RenderWorker::LayerCacheEntry>* cache,
                                        QMutex* cacheMutex)
{
    // Canvas size = the frame. Fallback to the base/first-media size for
    // documents predating the frame (frameW/H == 0).
    QSize outSize = (params.frameW > 0 && params.frameH > 0)
                  ? QSize(params.frameW, params.frameH)
                  : (source.isNull() ? QSize() : source.size());
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
    // Each layer renders independently, so render them all concurrently on
    // the thread pool, then composite sequentially in stack order.
    struct Job { QImage origSrc; Layer origLayer; QImage placed; };
    std::vector<Job> jobs;
    for (auto it = params.layers.rbegin(); it != params.layers.rend(); ++it) {
        if (!it->visible) continue;
        QImage src = layerSrc.contains(it->id) ? layerSrc.value(it->id) : source;
        if (src.isNull()) continue;
        jobs.push_back({src, *it, {}});
    }

    QtConcurrent::blockingMap(jobs, [outSize, cache, cacheMutex](Job& j) {
        // Cache key = everything that affects renderLayer's pixel output:
        // content settings + scale (via compensate/prerender), but NOT
        // position/rotation/flip, which only matter to placeOnFrame below.
        Layer contentKey = j.origLayer;
        contentKey.transform.xPct = contentKey.transform.yPct = contentKey.transform.rotation = 0.0f;
        contentKey.transform.flipH = contentKey.transform.flipV = false;
        const void* srcBits = static_cast<const void*>(j.origSrc.constBits());

        QImage rendered;
        bool hit = false;
        if (cache) {
            QMutexLocker lock(cacheMutex);
            auto it = cache->find(j.origLayer.id);
            if (it != cache->end() && it->key == contentKey
                && it->srcSize == j.origSrc.size() && it->srcBits == srcBits) {
                rendered = it->rendered;
                hit = true;
            }
        }
        if (!hit) {
            QImage wsrc = j.origSrc;
            Layer  wlayer = j.origLayer;
            compensateSymbolScale(wlayer);        // symbol sizes are frame px
            prerenderAtFrameRes(wsrc, wlayer);    // crisp symbols when scaled up
            rendered = renderLayer(wsrc, wlayer);
            if (cache) {
                QMutexLocker lock(cacheMutex);
                (*cache)[j.origLayer.id] = { contentKey, j.origSrc.size(), srcBits, rendered };
            }
        }

        // Placement always uses the LIVE transform (position/rotation/flip can
        // change every frame during a drag); only scalePct needs the same
        // frame-resolution adjustment prerenderAtFrameRes would have applied —
        // cheap to recompute, so a cache hit skips zero placement accuracy.
        LayerTransform placementTf = j.origLayer.transform;
        placementTf.scalePct = effectivePlacementScalePct(
            j.origLayer.kind, j.origLayer.transform.scalePct, j.origSrc.size());
        j.placed = placeOnFrame(rendered, placementTf, outSize);
    });

    for (const Job& j : jobs)
        BlendCompositor::compositeOver(canvas, j.placed, j.origLayer.blend);

    return canvas;
}

// ---------------------------------------------------------------------------
// SVG (vector) export
// ---------------------------------------------------------------------------

// The placement transform from placeOnFrame, as a matrix to set on the painter
// (so the renderers draw straight into frame space — no intermediate raster).
static QTransform layerMatrix(const LayerTransform& tf, QSize layerSize, QSize frame)
{
    const double s  = qMax(0.0001, double(tf.scalePct) / 100.0);
    const double cx = frame.width()  * 0.5 + double(tf.xPct) * frame.width();
    const double cy = frame.height() * 0.5 + double(tf.yPct) * frame.height();
    QTransform m;
    m.translate(cx, cy);
    m.rotate(tf.rotation);
    m.scale(s * (tf.flipH ? -1.0 : 1.0), s * (tf.flipV ? -1.0 : 1.0));
    m.translate(-layerSize.width() * 0.5, -layerSize.height() * 0.5);
    return m;
}

// True when the layer's fill is on (a removed fill paints nothing, matching
// renderLayerInto). Original always paints.
static bool layerFillEnabled(const Layer& l)
{
    switch (l.kind) {
        case LayerKind::Halftone: return l.halftone.tonal.enabled;
        case LayerKind::Dither:   return l.dither.tonal.enabled;
        case LayerKind::Ascii:    return l.ascii.tonal.enabled;
        case LayerKind::Original: return true;
    }
    return true;
}

bool RenderWorker::renderDocumentToSvg(const QString& path, const QImage& source,
                                       const SessionParams& params,
                                       const QHash<int, QImage>& layerSrc)
{
    QSize frame = (params.frameW > 0 && params.frameH > 0)
                ? QSize(params.frameW, params.frameH)
                : (source.isNull() ? QSize() : source.size());
    if (frame.isEmpty())
        for (const Layer& l : params.layers) {
            const QImage s = layerSrc.value(l.id);
            if (!s.isNull()) { frame = s.size(); break; }
        }
    if (frame.isEmpty()) return false;

    QSvgGenerator gen;
    gen.setFileName(path);
    gen.setSize(frame);
    gen.setViewBox(QRect(QPoint(0, 0), frame));
    gen.setTitle("ULTRA Ditherer");

    QPainter p(&gen);

    if (params.backgroundOpacity > 0.0f && params.background.alpha() > 0) {
        QColor bg = params.background;
        bg.setAlphaF(params.backgroundOpacity);
        p.fillRect(QRect(QPoint(0, 0), frame), bg);
    }

    // Stored top→bottom; paint bottom→top.
    for (auto it = params.layers.rbegin(); it != params.layers.rend(); ++it) {
        Layer layer = *it;
        compensateSymbolScale(layer);   // symbol sizes are frame px (matches raster)
        if (!layer.visible || !layerFillEnabled(layer)) continue;
        const QImage src = layerSrc.contains(layer.id) ? layerSrc.value(layer.id) : source;
        if (src.isNull()) continue;

        // Blend modes and the Original photo can't be expressed as SVG vectors;
        // rasterise those layers and embed them so the output still matches.
        const bool vectorable = (layer.blend == BlendMode::Normal)
                             && (layer.kind == LayerKind::Halftone
                              || layer.kind == LayerKind::Dither
                              || layer.kind == LayerKind::Ascii);
        if (!vectorable) {
            const QImage placed = placeOnFrame(renderLayer(src, layer), layer.transform, frame);
            p.save();
            p.resetTransform();
            p.drawImage(0, 0, placed);
            p.restore();
            continue;
        }

        const QImage adjusted  = ImageAdjuster::apply(src, layer.adjustments);
        const QImage forRender = flattenOntoWhite(adjusted);   // transparent = paper
        const QSize  ls        = adjusted.size();

        p.save();
        p.setTransform(layerMatrix(layer.transform, ls, frame));
        switch (layer.kind) {
            case LayerKind::Halftone: {
                // Match the raster path's inputDpi supersampling, or the grid
                // comes out ~dpi/72× coarser (much less dense) than the preview.
                const HalftoneSettings& hs = layer.halftone;
                float scale = qBound(18, hs.inputDpi, 300) / 72.0f;
                const int maxDim = qMax(forRender.width(), forRender.height());
                if (maxDim * scale > 6000.0f) scale = 6000.0f / maxDim;
                if (maxDim * scale < 16.0f)   scale = 16.0f / maxDim;
                HalftoneRenderer r;
                if (qAbs(scale - 1.0f) < 0.02f) {
                    r.renderVector(forRender, p, hs);
                } else {
                    QImage work = forRender.scaled(qMax(8, qRound(forRender.width()  * scale)),
                                                   qMax(8, qRound(forRender.height() * scale)),
                                                   Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    p.save();
                    p.scale(qreal(forRender.width())  / work.width(),
                            qreal(forRender.height()) / work.height());
                    // Same DPI compensation as the raster path: spacing is
                    // output px, DPI only changes sampling resolution.
                    HalftoneSettings hw = hs;
                    const float kx = float(work.width()) / forRender.width();
                    hw.grid.spacing      *= kx;
                    hw.grid.pointSpacing *= kx;
                    r.renderVector(work, p, hw);
                    p.restore();
                }
                break;
            }
            case LayerKind::Ascii:    AsciiRenderer::render(forRender, p, layer.ascii); break;
            case LayerKind::Dither:   DitherRenderer::paintMergedRects(forRender, layer.dither, p,
                                                                       ls.width(), ls.height()); break;
            default: break;
        }
        p.restore();
    }

    p.end();
    return true;
}

int RenderWorker::estimateSvgElements(const QImage& source, const SessionParams& params,
                                      const QHash<int, QImage>& layerSrc)
{
    long long total = 0;
    for (const Layer& layer : params.layers) {
        if (!layer.visible || !layerFillEnabled(layer)) continue;
        if (layer.blend != BlendMode::Normal) continue;   // rasterised, not vector nodes
        const QImage src = layerSrc.contains(layer.id) ? layerSrc.value(layer.id) : source;
        if (src.isNull()) continue;
        const QImage adjusted = ImageAdjuster::apply(src, layer.adjustments);
        const int w = adjusted.width(), h = adjusted.height();
        // Symbol pitch is in frame px (compensated), so a scaled-up layer packs
        // more elements per source pixel: counts grow with the placement scale².
        // DPI no longer changes the pitch, so it dropped out of the estimate.
        const double s  = qMax(0.0001, double(layer.transform.scalePct) / 100.0);
        const double s2 = s * s;
        switch (layer.kind) {
            case LayerKind::Halftone: {
                total += (long long)(HalftoneRenderer::estimateDotCount(adjusted, layer.halftone)
                                     * s2);
                break;
            }
            case LayerKind::Ascii: {
                const int cell = qBound(3, layer.ascii.cellSize, 128);
                total += (long long)(((long long)(w / cell + 1) * (h / cell + 1)) * s2);
                break;
            }
            case LayerKind::Dither: {   // worst case = un-merged cells
                const int ps = qBound(1, layer.dither.pixelSize, 32);
                total += (long long)(((long long)(w / ps + 1) * (h / ps + 1)) * s2);
                break;
            }
            default: break;
        }
    }
    return int(qMin<long long>(total, INT_MAX));
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

void RenderWorker::requestFullRender(const QImage& source, const SessionParams& params,
                                     const QHash<int, QImage>& layerSrc)
{
    m_sourceImage  = source;
    m_latestParams = params;
    m_layerSrc     = layerSrc;
    m_fullTimer.start();   // full pass only; no launchFast(), so nothing flashes
}

// Downscale every per-layer media image by `scale` (preview only), so layers
// that draw their own clip shrink in step with the frame.
static QHash<int, QImage> scaledLayerSrc(const QHash<int, QImage>& src, float scale)
{
    if (qAbs(scale - 1.0f) < 0.001f) return src;   // also rescale when scale > 1
    QHash<int, QImage> out;
    for (auto it = src.begin(); it != src.end(); ++it) {
        const QImage& im = it.value();
        out.insert(it.key(), im.isNull() ? im
                   : im.scaled(qMax(1, qRound(im.width()  * scale)),
                               qMax(1, qRound(im.height() * scale)),
                               Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    return out;
}

QImage RenderWorker::renderPreview(const QImage& source, const SessionParams& params, int maxPx,
                                   const QHash<int, QImage>& layerSrc)
{
    const int maxDim = qMax(source.width(), source.height());
    if (source.isNull() || maxDim <= maxPx || maxDim <= 0)
        return renderDocument(source, params, layerSrc);

    const float k = float(maxPx) / float(maxDim);
    const QImage small = source.scaled(maxPx, maxPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    SessionParams p = scaledForPreview(params, k);
    // Keep halftone dot scale consistent with the full render (mirrors launchFast).
    for (Layer& l : p.layers) {
        if (l.kind != LayerKind::Halftone) continue;
        const float requested = qBound(18, l.halftone.inputDpi, 300) / 72.0f;
        const float maxBySize = 6000.0f / float(maxDim);
        const float minBySize = 16.0f   / float(maxDim);
        const float eff = qBound(minBySize, requested, maxBySize);
        l.halftone.inputDpi = qBound(18, qRound(eff * 72.0f), 300);
    }
    return renderDocument(small, p, scaledLayerSrc(layerSrc, k));
}

QImage RenderWorker::renderPreviewCached(const QImage& source, const SessionParams& params, int maxPx,
                                         const QHash<int, QImage>& layerSrc)
{
    const int maxDim = qMax(source.width(), source.height());
    if (source.isNull() || maxDim <= maxPx || maxDim <= 0)
        return renderDocumentImpl(source, params, layerSrc, &m_layerCache, &m_layerCacheMutex);

    const float k = float(maxPx) / float(maxDim);
    const QImage small = source.scaled(maxPx, maxPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    SessionParams p = scaledForPreview(params, k);
    for (Layer& l : p.layers) {
        if (l.kind != LayerKind::Halftone) continue;
        const float requested = qBound(18, l.halftone.inputDpi, 300) / 72.0f;
        const float maxBySize = 6000.0f / float(maxDim);
        const float minBySize = 16.0f   / float(maxDim);
        const float eff = qBound(minBySize, requested, maxBySize);
        l.halftone.inputDpi = qBound(18, qRound(eff * 72.0f), 300);
    }
    return renderDocumentImpl(small, p, scaledLayerSrc(layerSrc, k), &m_layerCache, &m_layerCacheMutex);
}

SessionParams RenderWorker::scaledForPreview(const SessionParams& params, float scale)
{
    SessionParams p = params;
    // The frame is the output canvas; scale it (and the per-pixel params) so the
    // whole document — frame + transformed layers — shrinks uniformly.
    p.frameW = qMax(1, qRound(p.frameW * scale));
    p.frameH = qMax(1, qRound(p.frameH * scale));
    for (Layer& l : p.layers) {
        l.ascii.cellSize   = qMax(3, int(l.ascii.cellSize * scale));
        l.dither.pixelSize = qMax(1, qRound(l.dither.pixelSize * scale));
        // Halftone dot pitch is a fixed pixel value: scale it with the image too,
        // or dots stay full-size on the shrunk preview and look ~1/scale too big.
        l.halftone.grid.spacing = qMax(2.0f, l.halftone.grid.spacing * scale);
        l.adjustments.sharpenRadius = qMax(1, qRound(l.adjustments.sharpenRadius * scale));
    }
    return p;
}

void RenderWorker::launchFast()
{
    if (m_sourceImage.isNull()) return;

    // Downscale the whole document so neither the source render nor the frame
    // composite exceeds the interactive budget — both are capped.
    const int srcMax   = qMax(m_sourceImage.width(), m_sourceImage.height());
    const int frameMax = qMax(qMax(1, m_latestParams.frameW), m_latestParams.frameH);
    const int docMax   = qMax(srcMax, frameMax);
    const float k = (docMax > m_interactivePx) ? float(m_interactivePx) / float(docMax) : 1.0f;

    QImage src = m_sourceImage;
    SessionParams p = m_latestParams;
    QHash<int, QImage> ls = m_layerSrc;

    if (k < 1.0f) {
        src = m_sourceImage.scaled(qMax(1, qRound(m_sourceImage.width()  * k)),
                                   qMax(1, qRound(m_sourceImage.height() * k)),
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p  = scaledForPreview(m_latestParams, k);   // scales frame + per-pixel params
        ls = scaledLayerSrc(m_layerSrc, k);

        // Keep preview and full render at the same effective halftone scale.
        for (Layer& l : p.layers) {
            if (l.kind != LayerKind::Halftone) continue;
            const float requestedScale = qBound(18, l.halftone.inputDpi, 300) / 72.0f;
            const float maxBySize = 6000.0f / float(srcMax);
            const float minBySize = 16.0f   / float(srcMax);
            const float eff = qBound(minBySize, requestedScale, maxBySize);
            l.halftone.inputDpi = qBound(18, qRound(eff * 72.0f), 300);
        }
    }

    emit renderStarted(true);

    m_fastWatcher.setFuture(QtConcurrent::run([this, src, p, ls]() {
        return renderDocumentInteractive(src, p, ls);
    }));
}

void RenderWorker::launchFull()
{
    if (m_sourceImage.isNull()) return;

    QImage src = m_sourceImage;
    SessionParams p = m_latestParams;
    QHash<int,QImage> ls = m_layerSrc;

    // Zoom-driven supersample: render the whole document larger so the vector
    // symbols (halftone dots / ascii glyphs) are drawn at the displayed
    // resolution instead of upscaling a frame-sized raster. Capped to a pixel
    // budget so a deep zoom can't blow up memory / stall the export-grade pass.
    const int frameMax = qMax(1, qMax(p.frameW, p.frameH));
    const float ss = qMin(m_fullQualityScale, float(FULL_QUALITY_MAX_PX) / float(frameMax));
    if (ss > 1.01f) {
        src = m_sourceImage.scaled(qMax(1, qRound(m_sourceImage.width()  * ss)),
                                   qMax(1, qRound(m_sourceImage.height() * ss)),
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p  = scaledForPreview(m_latestParams, ss);   // frame + per-pixel params up
        ls = scaledLayerSrc(m_layerSrc, ss);          // per-layer media up
    }

    emit renderStarted(false);

    m_fullWatcher.setFuture(QtConcurrent::run([this, src, p, ls]() {
        return renderDocumentInteractive(src, p, ls);
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
