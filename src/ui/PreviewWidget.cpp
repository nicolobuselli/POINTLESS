#include "PreviewWidget.h"
#include "GpuCanvasWidget.h"
#include "Widgets.h"
#include "../core/AnimParams.h"   // locParamLabel

#include <QPainter>
#include <QPaintEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QTransform>
#include <QtMath>
#include <QLineF>
#include <QPainterPath>
#include <QKeyEvent>
#include <QApplication>

namespace {

QStringList imagePathsFromMime(const QMimeData* mime)
{
    static const QStringList exts = { "png", "jpg", "jpeg", "bmp", "webp", "gif", "tif", "tiff" };
    QStringList out;
    if (!mime || !mime->hasUrls()) return out;
    for (const QUrl& url : mime->urls()) {
        const QString p = url.toLocalFile();
        if (!p.isEmpty() && exts.contains(QFileInfo(p).suffix().toLower()))
            out << p;
    }
    return out;
}

constexpr double kHandleHit    = 9.0;   // px radius for corner hit-test
constexpr double kRotHandleHit = 11.0;
constexpr double kRotArm       = 28.0;  // px from top edge to rotation handle
constexpr double kLocRingHit   = 6.0;   // px hit tolerance for the loc radius/falloff rings
constexpr double kLocDotHit    = 9.0;   // px radius for the loc centre-dot hit/hover test

// Rotation handle position (widget space) given the widget-space quad
// [TL,TR,BR,BL] and the widget-space centre.
QPointF rotationHandleWidget(const QPolygonF& q, QPointF centre)
{
    const QPointF topMid = (q[0] + q[1]) * 0.5;
    QPointF up = topMid - centre;
    const double len = std::hypot(up.x(), up.y());
    if (len > 1e-6) up /= len;
    return topMid + up * kRotArm;
}

} // namespace

// Transparent child painted ABOVE the GPU canvas: all the QPainter chrome
// (selection outlines, transform handles, loc dots, rubber band, status).
// Mouse-transparent — PreviewWidget keeps every event exactly as before.
class PreviewOverlay : public QWidget
{
public:
    explicit PreviewOverlay(PreviewWidget* owner)
        : QWidget(owner), m_owner(owner)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        m_owner->paintOverlays(p);
    }
private:
    PreviewWidget* m_owner;
};

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    setMinimumSize(300, 200);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);   // needed for Backspace to delete a selected loc dot
    setMouseTracking(true);            // hover-grow the passive loc dot without a button held
}

void PreviewWidget::initGpu()
{
    if (m_canvas) return;
    m_canvas  = new GpuCanvasWidget(this);   // created first → stacked below…
    m_overlay = new PreviewOverlay(this);    // …the chrome overlay
    m_canvas->setGeometry(rect());
    m_overlay->setGeometry(rect());
    m_canvas->show();
    m_overlay->show();
    m_gpuActive = true;
    updateScaled();
    rerouteGpu();
    update();
}

void PreviewWidget::setGpuActive(bool on)
{
    if (m_gpuActive == on) return;
    m_gpuActive = on;
    if (m_canvas)  m_canvas->setVisible(on);
    if (m_overlay) m_overlay->setVisible(on);
    updateScaled();
    if (on) rerouteGpu();
    update();
}

void PreviewWidget::setGpuPackage(const GpuFramePackage& pkg)
{
    m_lastPkg = pkg;
    m_lastWasPackage = true;
    if (m_gpuActive) rerouteGpu();
}

// Decide what the canvas displays and keep the fit math in sync.
void PreviewWidget::rerouteGpu()
{
    if (!m_gpuActive || !m_canvas) return;
    if (m_showOriginal && !m_originalImage.isNull()) {
        m_viewSrcSize = m_originalImage.size();
        m_canvas->showImage(m_originalImage);
    } else if (m_lastWasPackage && m_lastPkg.valid) {
        m_viewSrcSize = m_lastPkg.frame;
        m_canvas->showPackage(m_lastPkg);
    } else if (!m_image.isNull()) {
        m_viewSrcSize = m_image.size();
        m_canvas->showImage(m_image);
    } else {
        m_viewSrcSize = {};
        m_canvas->showImage(QImage());
    }
    pushViewRect();
    if (m_overlay) m_overlay->update();
}

QSize PreviewWidget::viewSizePx() const
{
    if (!m_gpuActive)
        return m_scaled.size();
    if (m_viewSrcSize.isEmpty()) return {};
    const QSize target(qMax(1, int(size().width()  * m_zoomFactor)),
                       qMax(1, int(size().height() * m_zoomFactor)));
    return m_viewSrcSize.scaled(target, Qt::KeepAspectRatio);
}

void PreviewWidget::pushViewRect()
{
    if (!m_canvas) return;
    const QSize vs = viewSizePx();
    if (vs.isEmpty()) { m_canvas->setViewRect(QRectF()); return; }
    m_canvas->setViewRect(QRectF(imageOrigin(), QSizeF(vs)));
}

void PreviewWidget::setLocPoints(const QVector<LocEntry>& pts, QSize frame)
{
    // Keep the active/hovered selection stable across rebuilds (this is also
    // called mid-drag, echoing back the point we just emitted).
    const LocParam activeParam = (m_locActive >= 0 && m_locActive < m_locPts.size())
        ? m_locPts[m_locActive].param : LocParam::Count;
    m_locPts   = pts;
    m_locFrame = frame;
    m_locActive = -1;
    m_locHover  = -1;
    for (int i = 0; i < m_locPts.size(); ++i)
        if (m_locPts[i].param == activeParam) { m_locActive = i; break; }
    if (m_locActive < 0 && m_locDrag != LocDrag::None) m_locDrag = LocDrag::None;
    update();
}

void PreviewWidget::setLocOverlayVisible(bool on)
{
    m_locOverlayOn = on;
    if (!on) { m_locActive = -1; m_locHover = -1; m_locDrag = LocDrag::None; }
    update();
}

// Topmost (last-drawn) dot whose centre is within the hit radius, -1 if none.
int PreviewWidget::locDotHit(QPointF widgetPos) const
{
    for (int i = m_locPts.size() - 1; i >= 0; --i) {
        const QPointF centre = frameToWidget(locCentreFrame(m_locPts[i].pt));
        if (QLineF(widgetPos, centre).length() <= kLocDotHit) return i;
    }
    return -1;
}

QPointF PreviewWidget::locCentreFrame(const LocPoint& pt) const
{
    return QPointF(pt.posX * m_locFrame.width(), pt.posY * m_locFrame.height());
}

double PreviewWidget::locRadiusFramePx(const LocPoint& pt) const
{
    return double(pt.radius) * qMin(m_locFrame.width(), m_locFrame.height());
}

double PreviewWidget::locInnerFramePx(const LocPoint& pt) const
{
    return locRadiusFramePx(pt) * (1.0 - qBound(0.0f, pt.falloff, 1.0f));
}

void PreviewWidget::setImage(const QImage& img)
{
    m_image = img;
    m_lastWasPackage = false;   // a flattened frame supersedes the last package
    updateScaled();
    if (m_gpuActive) rerouteGpu();
    update();
}

void PreviewWidget::setOriginalImage(const QImage& img)
{
    m_originalImage = img;
    if (m_showOriginal) {
        updateScaled();
        if (m_gpuActive) rerouteGpu();
        update();
    }
}

void PreviewWidget::setStatus(const QString& text)
{
    m_status = text;
    update();
}

void PreviewWidget::resetZoom()
{
    m_zoomFactor = 0.7;   // frame fits at 70% of the canvas, not edge-to-edge
    m_panOffset = {};
    m_dragging = false;
    m_dragButton = Qt::NoButton;
    updateScaled();
    update();
}

void PreviewWidget::setShowOriginal(bool show)
{
    if (m_showOriginal == show) return;
    m_showOriginal = show;
    updateScaled();
    if (m_gpuActive) rerouteGpu();
    update();
}

void PreviewWidget::setPanMode(bool enabled)
{
    if (m_panMode == enabled) return;
    m_panMode = enabled;
    if (!enabled && m_dragButton == Qt::LeftButton) {
        m_dragging = false;
        m_dragButton = Qt::NoButton;
    }
    setCursor(enabled ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void PreviewWidget::setActiveTransform(const LayerTransform& tf, QSize layerNative,
                                       QSize frame, bool transformable)
{
    // Don't clobber the live value while the user is dragging a handle.
    if (m_tfDrag != TfDrag::None || m_groupDrag) return;

    m_tf            = tf;
    m_layerNative   = layerNative;
    m_frame         = frame;
    m_transformable = transformable && !layerNative.isEmpty() && !frame.isEmpty();
    update();
}

void PreviewWidget::setCanvasLayers(const QVector<CanvasLayer>& layers, QSize frame)
{
    m_canvasLayers = layers;
    if (!frame.isEmpty()) m_frame = frame;
    update();
}

void PreviewWidget::setSelection(const QSet<int>& selected, int activeId)
{
    m_selection = selected;
    m_activeId  = activeId;
    update();
}

double PreviewWidget::imageScale() const
{
    const QSize vs = viewSizePx();
    if (vs.isEmpty() || m_frame.width() <= 0) return 0.0;
    return double(vs.width()) / m_frame.width();
}

QPointF PreviewWidget::imageOrigin() const
{
    const QSize vs = viewSizePx();
    return QPointF((width()  - vs.width())  / 2.0 + m_panOffset.x(),
                   (height() - vs.height()) / 2.0 + m_panOffset.y());
}

QPointF PreviewWidget::frameToWidget(QPointF f) const
{
    const double k = imageScale();
    return imageOrigin() + QPointF(f.x() * k, f.y() * k);
}

QPointF PreviewWidget::widgetToFrame(QPointF w) const
{
    const double k = imageScale();
    if (k <= 0.0) return {};
    const QPointF o = imageOrigin();
    return QPointF((w.x() - o.x()) / k, (w.y() - o.y()) / k);
}

QPointF PreviewWidget::layerCentreFrame() const
{
    return QPointF(m_frame.width()  * 0.5 + double(m_tf.xPct) * m_frame.width(),
                   m_frame.height() * 0.5 + double(m_tf.yPct) * m_frame.height());
}

QPolygonF PreviewWidget::quadFrame(const LayerTransform& tf, QSize native) const
{
    const double s     = qMax(0.0001, double(tf.scalePct) / 100.0);
    const double halfW = native.width()  * 0.5 * s;
    const double halfH = native.height() * 0.5 * s;
    const QPointF c(m_frame.width()  * 0.5 + double(tf.xPct) * m_frame.width(),
                    m_frame.height() * 0.5 + double(tf.yPct) * m_frame.height());
    QTransform m;
    m.translate(c.x(), c.y());
    m.rotate(tf.rotation);
    QPolygonF q;
    q << m.map(QPointF(-halfW, -halfH)) << m.map(QPointF(halfW, -halfH))
      << m.map(QPointF(halfW,  halfH)) << m.map(QPointF(-halfW, halfH));
    return q;
}

QPolygonF PreviewWidget::layerQuadFrame() const
{
    return quadFrame(m_tf, m_layerNative);
}

QPolygonF PreviewWidget::quadWidget(const LayerTransform& tf, QSize native) const
{
    QPolygonF qw;
    for (const QPointF& f : quadFrame(tf, native)) qw << frameToWidget(f);
    return qw;
}

bool PreviewWidget::widgetInsideFrame(QPointF widgetPos) const
{
    if (m_frame.isEmpty()) return false;
    return QRectF(QPointF(0.0, 0.0), QSizeF(m_frame)).contains(widgetToFrame(widgetPos));
}

bool PreviewWidget::handlesVisible() const
{
    return m_transformable && !m_panMode && !m_showOriginal
        && m_selection.size() == 1 && m_selection.contains(m_activeId);
}

bool PreviewWidget::groupHandlesVisible() const
{
    return !m_panMode && !m_showOriginal && m_selection.size() >= 2;
}

QPointF PreviewWidget::centreFrame(const LayerTransform& tf) const
{
    return QPointF(m_frame.width()  * 0.5 + double(tf.xPct) * m_frame.width(),
                   m_frame.height() * 0.5 + double(tf.yPct) * m_frame.height());
}

QRectF PreviewWidget::layerBBoxAt(const QPointF& centre, const LayerTransform& tf, QSize native) const
{
    LayerTransform t = tf;
    t.xPct = m_frame.width()  > 0 ? float((centre.x() - m_frame.width()  * 0.5) / m_frame.width())  : 0.0f;
    t.yPct = m_frame.height() > 0 ? float((centre.y() - m_frame.height() * 0.5) / m_frame.height()) : 0.0f;
    return quadFrame(t, native).boundingRect();
}

// Nudge (dx,dy) that would snap bboxFrame's edges/midlines onto the frame's,
// when within a screen-constant threshold; zero on each axis with no match.
QPointF PreviewWidget::snapDeltaForBBox(const QRectF& bbox) const
{
    const double k = imageScale();
    if (k <= 0.0 || m_frame.isEmpty()) return {};
    const double thr = 8.0 / k;   // ~8 widget px, independent of zoom

    double bestDx = 0.0, bestDxDist = thr;
    const double dxCandidates[3] = {
        0.0                     - bbox.left(),
        double(m_frame.width()) - bbox.right(),
        m_frame.width() * 0.5   - (bbox.left() + bbox.right()) * 0.5,
    };
    for (double d : dxCandidates)
        if (std::abs(d) < bestDxDist) { bestDxDist = std::abs(d); bestDx = d; }

    double bestDy = 0.0, bestDyDist = thr;
    const double dyCandidates[3] = {
        0.0                      - bbox.top(),
        double(m_frame.height()) - bbox.bottom(),
        m_frame.height() * 0.5   - (bbox.top() + bbox.bottom()) * 0.5,
    };
    for (double d : dyCandidates)
        if (std::abs(d) < bestDyDist) { bestDyDist = std::abs(d); bestDy = d; }

    return QPointF(bestDx, bestDy);
}

QRectF PreviewWidget::groupBBoxFrame() const
{
    QRectF box;
    bool first = true;
    for (const CanvasLayer& cl : m_canvasLayers) {
        if (!m_selection.contains(cl.id)) continue;
        const QRectF b = quadFrame(cl.tf, cl.native).boundingRect();
        box = first ? b : box.united(b);
        first = false;
    }
    return first ? QRectF() : box;
}

QRectF PreviewWidget::groupRectWidget() const
{
    const QRectF f = groupBBoxFrame();
    if (f.isNull()) return {};
    // frameToWidget is scale+translate only, so the AABB maps to an AABB.
    return QRectF(frameToWidget(f.topLeft()), frameToWidget(f.bottomRight())).normalized();
}

const PreviewWidget::CanvasLayer* PreviewWidget::layerById(int id) const
{
    for (const CanvasLayer& cl : m_canvasLayers)
        if (cl.id == id) return &cl;
    return nullptr;
}

int PreviewWidget::hitTest(QPointF widgetPos) const
{
    if (!widgetInsideFrame(widgetPos)) return -1;
    // m_canvasLayers is top-first → first match is the top-most layer.
    for (const CanvasLayer& cl : m_canvasLayers)
        if (!cl.locked
            && quadWidget(cl.tf, cl.native).containsPoint(widgetPos, Qt::OddEvenFill))
            return cl.id;
    return -1;
}

const QImage& PreviewWidget::currentSource() const
{
    if (m_showOriginal && !m_originalImage.isNull()) return m_originalImage;
    return m_image;
}

void PreviewWidget::updateScaled()
{
    if (m_gpuActive) { m_scaled = {}; return; }   // GPU blits; no CPU rescale
    const QImage& source = currentSource();
    if (source.isNull()) { m_scaled = {}; return; }
    // Upscaling: nearest-neighbor preserves crisp symbol edges.
    // Downscaling: smooth avoids moiré on dense patterns.
    const QSize targetSize = QSize(qMax(1, int(size().width()  * m_zoomFactor)),
                                   qMax(1, int(size().height() * m_zoomFactor)));
    const bool upscaling = (source.width()  <= targetSize.width()
                         && source.height() <= targetSize.height());
    const auto mode = upscaling ? Qt::FastTransformation : Qt::SmoothTransformation;
    m_scaled = source.scaled(targetSize, Qt::KeepAspectRatio, mode);
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_canvas)  m_canvas->setGeometry(rect());
    if (m_overlay) m_overlay->setGeometry(rect());
    updateScaled();
    if (m_gpuActive) pushViewRect();
}

void PreviewWidget::wheelEvent(QWheelEvent* event)
{
    // Plain wheel over the active localization point's circle adjusts its
    // intensity (the ×scale multiplier) — the only knob the point has.
    if (!(event->modifiers() & Qt::ControlModifier)
        && locShown() && m_locActive >= 0 && m_locActive < m_locPts.size()) {
        LocPoint& lp = m_locPts[m_locActive].pt;
        const QPointF centre = frameToWidget(locCentreFrame(lp));
        const double dist    = QLineF(event->position(), centre).length();
        const double outerR  = locRadiusFramePx(lp) * imageScale();
        if (dist <= outerR + kLocRingHit) {
            const int delta = event->angleDelta().y();
            if (delta != 0) {
                const float step = (delta > 0) ? 1.1f : 1.0f / 1.1f;
                lp.scale = qBound(0.1f, lp.scale * step, 10.0f);
                update();
                emit localizationChanged(m_locPts[m_locActive].param, lp);
                emit localizationEditFinished();
            }
            event->accept();
            return;
        }
    }

    if (!(event->modifiers() & Qt::ControlModifier)) {
        event->ignore();
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->accept();
        return;
    }

    const double factorStep = (delta > 0) ? 1.12 : 1.0 / 1.12;
    const double prev = m_zoomFactor;
    m_zoomFactor = qBound(0.2, m_zoomFactor * factorStep, 8.0);
    updateScaled();
    update();

    setToolTip(QString("Zoom %1%").arg(int(m_zoomFactor * 100.0)));
    if (!qFuzzyCompare(prev, m_zoomFactor)) emit zoomChanged();
    event->accept();
}

void PreviewWidget::mousePressEvent(QMouseEvent* event)
{
    m_snapped = false;   // cleared here; Move drags re-derive it every move
    m_gestureMoved = false;   // set true once the mouse clears the jitter threshold below
    m_pressPos = event->position();
    const bool leftPan = m_panMode && event->button() == Qt::LeftButton;
    const bool middlePan = event->button() == Qt::MiddleButton;
    if (imageScale() > 0.0 && (leftPan || middlePan)) {
        m_dragging = true;
        m_dragButton = event->button();
        m_lastDragPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && !m_panMode && !m_showOriginal
        && imageScale() > 0.0 && !widgetInsideFrame(event->position())) {
        m_boxSelecting = true;
        m_boxAdditive  = (event->modifiers() & Qt::ShiftModifier);
        m_boxStart = m_boxCur = event->pos();
        event->accept();
        return;
    }

    // Selection + transform (left button, not in pan/original mode).
    if (event->button() == Qt::LeftButton && !m_panMode && !m_showOriginal
        && imageScale() > 0.0) {
        const QPointF pos = event->position();
        const Qt::KeyboardModifiers mods = event->modifiers();

        // -1. Localization dots of the active layer, if any are shown.
        //     Take priority over everything else on the canvas.
        if (locShown()) {
            const int hitDot = locDotHit(pos);   // passive dot under the cursor

            if (m_locActive >= 0) {
                // Active point: its rings are live.
                const LocPoint& lp   = m_locPts[m_locActive].pt;
                const QPointF centre = frameToWidget(locCentreFrame(lp));
                const double dist    = QLineF(pos, centre).length();
                const double outerR  = locRadiusFramePx(lp) * imageScale();
                const double innerR  = locInnerFramePx(lp)  * imageScale();

                // Nearest ring wins: at small radii (or falloff ≈ 0) the two
                // rings sit inside each other's hit tolerance, and testing
                // outer-first made the falloff ring ungrabbable. Ties (rings
                // coincident) break by side: inside → falloff, outside → radius.
                const double dOut = std::abs(dist - outerR);
                const double dIn  = std::abs(dist - innerR);
                if (dOut <= kLocRingHit || dIn <= kLocRingHit) {
                    const bool falloffWins = dIn < dOut || (dIn == dOut && dist < outerR);
                    m_locDrag = falloffWins ? LocDrag::Falloff : LocDrag::Radius;
                    setFocus(Qt::MouseFocusReason);
                    update(); event->accept(); return;
                }
                if (dist <= qMax(kLocDotHit, outerR)) {
                    m_locDrag = LocDrag::Move;
                    m_locGrabOffset = locCentreFrame(lp) - widgetToFrame(pos);
                    setFocus(Qt::MouseFocusReason);
                    update(); event->accept(); return;
                }
                m_locActive = -1;   // clicked outside its circle → deselect
                update();
                // …and fall through to the passive dots below.
            }

            if (hitDot >= 0) {
                // Passive dot: a hit both activates it (shows the rings) and
                // starts moving it in the same gesture.
                m_locActive     = hitDot;
                m_locHover      = -1;
                m_locDrag       = LocDrag::Move;
                m_locGrabOffset = locCentreFrame(m_locPts[hitDot].pt) - widgetToFrame(pos);
                setFocus(Qt::MouseFocusReason);
                update(); event->accept(); return;
            }
            // Miss → fall through to normal canvas handling.
        }

        // 0. Group gizmo for a multi-layer selection. Plain drags only — Shift/Ctrl
        //    still add/remove layers from the selection.
        if (groupHandlesVisible() && !(mods & (Qt::ShiftModifier | Qt::ControlModifier))) {
            const QRectF gw      = groupRectWidget();
            const QPointF framePt = widgetToFrame(pos);
            const QPointF pivot   = groupBBoxFrame().center();
            const QPointF topMid  = (gw.topLeft() + gw.topRight()) * 0.5;
            const QPointF rot     = topMid + QPointF(0, -kRotArm);

            auto beginGroup = [&](TfDrag mode) {
                m_groupDrag  = true;
                m_groupMode  = mode;
                m_groupPivot = pivot;
                m_groupStart.clear();
                for (const CanvasLayer& cl : m_canvasLayers)
                    if (m_selection.contains(cl.id)) m_groupStart.insert(cl.id, cl.tf);
            };

            if (QLineF(pos, rot).length() <= kRotHandleHit) {
                beginGroup(TfDrag::Rotate);
                m_startAngle = std::atan2(framePt.y() - pivot.y(), framePt.x() - pivot.x());
                event->accept();
                return;
            }
            const QPointF corners[4] = { gw.topLeft(), gw.topRight(),
                                         gw.bottomRight(), gw.bottomLeft() };
            for (const QPointF& corner : corners) {
                if (QLineF(pos, corner).length() <= kHandleHit) {
                    beginGroup(TfDrag::Scale);
                    m_startDist = qMax(1e-3, QLineF(pivot, framePt).length());
                    event->accept();
                    return;
                }
            }
            if (gw.contains(pos)) {
                beginGroup(TfDrag::Move);
                m_groupGrabFrame = framePt;
                setCursor(Qt::SizeAllCursor);
                event->accept();
                return;
            }
            // Outside the group box → fall through to normal click / box-select.
        }

        // 1. Rotation/scale handles of the single active layer take priority.
        if (handlesVisible()) {
            QPolygonF qw;
            for (const QPointF& f : layerQuadFrame()) qw << frameToWidget(f);
            const QPointF centre = frameToWidget(layerCentreFrame());
            m_tfStart = m_tf;
            const QPointF framePt = widgetToFrame(pos);
            const QPointF c = layerCentreFrame();

            if (QLineF(pos, rotationHandleWidget(qw, centre)).length() <= kRotHandleHit) {
                m_tfDrag = TfDrag::Rotate;
                m_startAngle = std::atan2(framePt.y() - c.y(), framePt.x() - c.x());
                event->accept();
                return;
            }
            for (const QPointF& corner : qw) {
                if (QLineF(pos, corner).length() <= kHandleHit) {
                    m_tfDrag = TfDrag::Scale;
                    m_startDist = qMax(1e-3, QLineF(c, framePt).length());
                    event->accept();
                    return;
                }
            }
        }

        const int hit = hitTest(pos);   // top-most layer under the cursor

        // Shift / Ctrl operate on the top-most layer under the cursor.
        if (hit >= 0 && (mods & Qt::ShiftModifier)) {        // add to selection
            QSet<int> sel = m_selection; sel.insert(hit);
            emit selectionChanged(sel, hit);
            event->accept();
            return;
        }
        if (hit >= 0 && (mods & Qt::ControlModifier)) {      // remove from selection
            QSet<int> sel = m_selection; sel.remove(hit);
            const int act = sel.contains(m_activeId) ? m_activeId
                          : (sel.isEmpty() ? -1 : *sel.begin());
            emit selectionChanged(sel, act);
            event->accept();
            return;
        }

        // Plain click. If the cursor is over an already-selected layer, act on
        // THAT one (so you can grab a selected image even when another sits on
        // top of it) — preferring the active layer. Otherwise pick the top-most.
        int target = -1;
        if (m_selection.contains(m_activeId)
            && quadWidget(m_tf, m_layerNative).containsPoint(pos, Qt::OddEvenFill)) {
            target = m_activeId;
        } else {
            for (const CanvasLayer& cl : m_canvasLayers)   // top-first
                if (m_selection.contains(cl.id)
                    && quadWidget(cl.tf, cl.native).containsPoint(pos, Qt::OddEvenFill)) {
                    target = cl.id;
                    break;
                }
        }
        const bool keepSelection = (target >= 0);   // clicked inside the selection
        if (target < 0) target = hit;               // else grab the top-most layer

        if (target >= 0) {
            // Begin a move drag; set local geometry up front so the move is
            // correct before MainWindow round-trips the selection back.
            const CanvasLayer* cl = layerById(target);
            if (cl) {
                m_tf            = cl->tf;
                m_layerNative   = cl->native;
                m_transformable = !cl->native.isEmpty();
                m_activeId      = target;
                if (!keepSelection) m_selection = { target };
                m_tfStart       = m_tf;
                const QPointF framePt = widgetToFrame(pos);
                m_grabOffset    = layerCentreFrame() - framePt;
                m_tfDrag        = TfDrag::Move;
                setCursor(Qt::SizeAllCursor);
            }
            emit selectionChanged(m_selection, target);
            update();
            event->accept();
            return;
        }

        // Empty area (outside every layer) → rubber-band box selection. A click
        // with no drag clears the selection (handled on release).
        m_boxSelecting = true;
        m_boxAdditive  = (mods & Qt::ShiftModifier);
        m_boxStart = m_boxCur = event->pos();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void PreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_gestureMoved) {
        // A physical mouse/trackpad click always has some jitter between press
        // and release — well past a couple of pixels. Without a real tolerance
        // that jitter alone still counts as a drag and fires the expensive
        // end-of-gesture full-quality render for a click that changed nothing;
        // barely visible on cheap modes, a noticeable stutter on slow ones
        // (Mosaic with many tiles). Qt's own click-vs-drag distance is the
        // right threshold — it's what every other Qt drag gesture uses.
        const double thr = qMax(1, QApplication::startDragDistance());
        const QPointF d = event->position() - m_pressPos;
        if (QPointF::dotProduct(d, d) > thr * thr)
            m_gestureMoved = true;
    }

    if (m_locDrag != LocDrag::None && m_locActive >= 0 && m_locActive < m_locPts.size()) {
        const QPointF framePt = widgetToFrame(event->position());
        LocPoint& lp = m_locPts[m_locActive].pt;
        switch (m_locDrag) {
            case LocDrag::Move: {
                const QPointF c = framePt + m_locGrabOffset;
                lp.posX = m_locFrame.width()  > 0 ? float(qBound(0.0, c.x() / m_locFrame.width(),  1.0)) : 0.5f;
                lp.posY = m_locFrame.height() > 0 ? float(qBound(0.0, c.y() / m_locFrame.height(), 1.0)) : 0.5f;
                break;
            }
            case LocDrag::Radius: {
                const double d    = QLineF(locCentreFrame(lp), framePt).length();
                const double base = qMin(m_locFrame.width(), m_locFrame.height());
                if (base > 0) lp.radius = float(qBound(0.01, d / base, 1.0));
                break;
            }
            case LocDrag::Falloff: {
                const double d      = QLineF(locCentreFrame(lp), framePt).length();
                const double outerR = locRadiusFramePx(lp);
                if (outerR > 1e-3) lp.falloff = float(qBound(0.0, 1.0 - d / outerR, 1.0));
                break;
            }
            default: break;
        }
        update();
        emit localizationChanged(m_locPts[m_locActive].param, lp);
        event->accept();
        return;
    }

    if (m_groupDrag) {
        const QPointF framePt = widgetToFrame(event->position());
        const QPointF P = m_groupPivot;
        QHash<int, LayerTransform> out;

        if (m_groupMode == TfDrag::Move) {
            QPointF d = framePt - m_groupGrabFrame;
            if (event->modifiers() & Qt::ShiftModifier) {
                if (std::abs(d.x()) >= std::abs(d.y())) d.setY(0.0);
                else                                     d.setX(0.0);
            }

            // Magnetic snap on the group's combined bounding box.
            {
                QRectF box; bool first = true;
                for (auto it = m_groupStart.cbegin(); it != m_groupStart.cend(); ++it) {
                    const CanvasLayer* cl = layerById(it.key());
                    if (!cl) continue;
                    const QRectF b = layerBBoxAt(centreFrame(it.value()) + d, it.value(), cl->native);
                    box = first ? b : box.united(b);
                    first = false;
                }
                const QPointF snap = first ? QPointF() : snapDeltaForBBox(box);
                m_snapped = (snap.x() != 0.0 || snap.y() != 0.0);
                d += snap;
            }

            for (auto it = m_groupStart.cbegin(); it != m_groupStart.cend(); ++it) {
                LayerTransform t = it.value();
                const QPointF c = centreFrame(t) + d;
                t.xPct = m_frame.width()  > 0 ? float((c.x() - m_frame.width()  * 0.5) / m_frame.width())  : 0.0f;
                t.yPct = m_frame.height() > 0 ? float((c.y() - m_frame.height() * 0.5) / m_frame.height()) : 0.0f;
                out.insert(it.key(), t);
            }
        } else if (m_groupMode == TfDrag::Scale) {
            const double f = QLineF(P, framePt).length() / m_startDist;
            for (auto it = m_groupStart.cbegin(); it != m_groupStart.cend(); ++it) {
                LayerTransform t = it.value();
                t.scalePct = qBound(0.1f, float(it.value().scalePct * f), 100000.0f);
                const QPointF c = P + (centreFrame(it.value()) - P) * f;
                t.xPct = m_frame.width()  > 0 ? float((c.x() - m_frame.width()  * 0.5) / m_frame.width())  : 0.0f;
                t.yPct = m_frame.height() > 0 ? float((c.y() - m_frame.height() * 0.5) / m_frame.height()) : 0.0f;
                out.insert(it.key(), t);
            }
        } else if (m_groupMode == TfDrag::Rotate) {
            const double a   = std::atan2(framePt.y() - P.y(), framePt.x() - P.x());
            const double rad = a - m_startAngle;
            const double cs = std::cos(rad), sn = std::sin(rad);
            const float dDeg = float(qRadiansToDegrees(rad));
            for (auto it = m_groupStart.cbegin(); it != m_groupStart.cend(); ++it) {
                LayerTransform t = it.value();
                const QPointF rel = centreFrame(it.value()) - P;
                const QPointF c(P.x() + rel.x() * cs - rel.y() * sn,
                                P.y() + rel.x() * sn + rel.y() * cs);
                float r = it.value().rotation + dDeg;
                r = std::fmod(r, 360.0f);
                if (r > 180.0f)  r -= 360.0f;
                if (r < -180.0f) r += 360.0f;
                t.rotation = r;
                t.xPct = m_frame.width()  > 0 ? float((c.x() - m_frame.width()  * 0.5) / m_frame.width())  : 0.0f;
                t.yPct = m_frame.height() > 0 ? float((c.y() - m_frame.height() * 0.5) / m_frame.height()) : 0.0f;
                out.insert(it.key(), t);
            }
        }
        update();
        emit groupTransformChanged(out);
        event->accept();
        return;
    }

    if (m_tfDrag != TfDrag::None) {
        const QPointF framePt = widgetToFrame(event->position());
        const QPointF c = layerCentreFrame();
        switch (m_tfDrag) {
            case TfDrag::Move: {
                QPointF nc = framePt + m_grabOffset;
                if (event->modifiers() & Qt::ShiftModifier) {
                    // Constrain to whichever axis best matches the total drag
                    // direction from where the gesture started (not the
                    // instantaneous mouse delta), so the axis doesn't flicker.
                    const QPointF startC = centreFrame(m_tfStart);
                    QPointF d = nc - startC;
                    if (std::abs(d.x()) >= std::abs(d.y())) d.setY(0.0);
                    else                                     d.setX(0.0);
                    nc = startC + d;
                }
                const QPointF snap = snapDeltaForBBox(layerBBoxAt(nc, m_tf, m_layerNative));
                m_snapped = (snap.x() != 0.0 || snap.y() != 0.0);
                nc += snap;
                m_tf.xPct = m_frame.width()  > 0 ? float((nc.x() - m_frame.width()  * 0.5) / m_frame.width())  : 0.0f;
                m_tf.yPct = m_frame.height() > 0 ? float((nc.y() - m_frame.height() * 0.5) / m_frame.height()) : 0.0f;
                break;
            }
            case TfDrag::Scale: {
                const double d = QLineF(c, framePt).length();
                m_tf.scalePct = qBound(0.1f, float(m_tfStart.scalePct * d / m_startDist), 100000.0f);
                break;
            }
            case TfDrag::Rotate: {
                const double a = std::atan2(framePt.y() - c.y(), framePt.x() - c.x());
                float r = m_tfStart.rotation + float(qRadiansToDegrees(a - m_startAngle));
                // Snap to the nearest multiple of 90° when close to it.
                const float nearest90 = std::round(r / 90.0f) * 90.0f;
                if (std::abs(r - nearest90) <= 7.0f) r = nearest90;
                r = std::fmod(r, 360.0f);
                if (r > 180.0f)  r -= 360.0f;
                if (r < -180.0f) r += 360.0f;
                m_tf.rotation = r;
                break;
            }
            default: break;
        }
        update();
        emit transformChanged(m_tf);
        event->accept();
        return;
    }

    if (m_boxSelecting) {
        m_boxCur = event->pos();
        update();
        event->accept();
        return;
    }

    if (m_dragging && event->buttons().testFlag(m_dragButton)) {
        const QPoint delta = event->pos() - m_lastDragPos;
        m_panOffset += delta;
        m_lastDragPos = event->pos();
        update();
        event->accept();
        return;
    }

    // Passive-dot hover: grow the dot a bit (and name it) so it's easy to grab.
    if (locShown() && !m_panMode) {
        int hover = locDotHit(event->position());
        if (hover == m_locActive) hover = -1;   // the active dot already shows its rings
        if (hover != m_locHover) {
            m_locHover = hover;
            setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    } else if (m_locHover != -1) {
        m_locHover = -1;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    m_snapped = false;
    if (m_locDrag != LocDrag::None && event->button() == Qt::LeftButton) {
        m_locDrag = LocDrag::None;
        // A plain click that activates a dot (no actual drag) still hits this
        // branch — only a real move needs the render/undo-key it triggers.
        if (m_gestureMoved) emit localizationEditFinished();
        event->accept();
        return;
    }

    if (m_groupDrag && event->button() == Qt::LeftButton) {
        m_groupDrag = false;
        m_groupMode = TfDrag::None;
        setCursor(m_panMode ? Qt::OpenHandCursor : Qt::ArrowCursor);
        if (m_gestureMoved) emit transformEditFinished();
        event->accept();
        return;
    }

    if (m_tfDrag != TfDrag::None && event->button() == Qt::LeftButton) {
        m_tfDrag = TfDrag::None;
        setCursor(m_panMode ? Qt::OpenHandCursor : Qt::ArrowCursor);
        // Selecting a layer with a plain click (no drag) also arms Move above
        // (mousePressEvent) — without this guard every click fired a full
        // re-render for zero actual change.
        if (m_gestureMoved) emit transformEditFinished();   // gesture over → MainWindow does a full render
        event->accept();
        return;
    }

    if (m_boxSelecting && event->button() == Qt::LeftButton) {
        m_boxSelecting = false;
        const QRect r = QRect(m_boxStart, m_boxCur).normalized();

        if (r.width() < 3 && r.height() < 3) {
            // No real drag → an empty-area click clears the selection.
            if (!m_boxAdditive) emit selectionChanged(QSet<int>{}, -1);
        } else {
            // Drag left → "crossing": any layer that partially overlaps.
            // Drag right → "window": only layers fully inside the box.
            const bool crossing = m_boxCur.x() < m_boxStart.x();
            QPainterPath rectPath; rectPath.addRect(QRectF(r));
            QSet<int> sel = m_boxAdditive ? m_selection : QSet<int>{};
            for (const CanvasLayer& cl : m_canvasLayers) {
                if (cl.locked) continue;
                QPainterPath pp; pp.addPolygon(quadWidget(cl.tf, cl.native)); pp.closeSubpath();
                const bool inside = crossing ? rectPath.intersects(pp) : rectPath.contains(pp);
                if (inside) sel.insert(cl.id);
            }
            const int act = sel.contains(m_activeId) ? m_activeId
                          : (sel.isEmpty() ? -1 : *sel.begin());
            emit selectionChanged(sel, act);
        }
        update();
        event->accept();
        return;
    }

    if (m_dragging && event->button() == m_dragButton) {
        m_dragging = false;
        m_dragButton = Qt::NoButton;
        setCursor(m_panMode ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PreviewWidget::keyPressEvent(QKeyEvent* event)
{
    if (locShown() && m_locActive >= 0 && event->key() == Qt::Key_Backspace) {
        const LocParam p = m_locPts[m_locActive].param;
        m_locPts.removeAt(m_locActive);
        m_locActive = -1;
        m_locHover  = -1;
        emit localizationDeleteRequested(p);
        update();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PreviewWidget::paintLocHandles(QPainter& p)
{
    p.setRenderHint(QPainter::Antialiasing, true);

    QFont labelFont = p.font();
    labelFont.setPixelSize(11);
    p.setFont(labelFont);

    for (int i = 0; i < m_locPts.size(); ++i) {
        const LocPoint& lp   = m_locPts[i].pt;
        const QPointF centre = frameToWidget(locCentreFrame(lp));
        const bool active    = (i == m_locActive);
        const bool hovered   = (i == m_locHover);

        // Active: radius/falloff rings are live and draggable. Passive: just
        // the plain dot, click it to activate.
        if (active) {
            const double outerR = locRadiusFramePx(lp) * imageScale();
            const double innerR = locInnerFramePx(lp)  * imageScale();

            QPen outerPen(QColor("#FD5A1F"));
            outerPen.setWidthF(1.4);
            p.setPen(outerPen);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(centre, outerR, outerR);

            QPen innerPen(QColor("#F0F0F0"));
            innerPen.setStyle(Qt::DashLine);
            innerPen.setWidthF(1.0);
            p.setPen(innerPen);
            p.drawEllipse(centre, innerR, innerR);
        }

        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#F0F0F0"));
        const double dotR = (!active && hovered) ? 6.0 : 4.0;
        p.drawEllipse(centre, dotR, dotR);

        // Name tag on the active/hovered dot, so overlapping points from
        // different parameters stay tellable apart. The active one also shows
        // its intensity (scroll over the circle to change it).
        if (active || hovered) {
            QString label = QString::fromUtf8(locParamLabel(m_locPts[i].param));
            // Intensity shown only once it differs from neutral (scroll to change).
            if (active && std::abs(lp.scale - 1.0f) > 0.01f)
                label += QString::fromUtf8(" ×%1").arg(double(lp.scale), 0, 'g', 3);
            const QPointF at(centre.x() + 10.0, centre.y() - 8.0);
            p.setPen(QColor(0, 0, 0, 160));                   // soft halo
            p.drawText(at + QPointF(1, 1), label);
            p.setPen(active ? QColor("#FD5A1F") : QColor("#F0F0F0"));
            p.drawText(at, label);
        }
    }
}

void PreviewWidget::paintHandles(QPainter& p)
{
    QPolygonF qw;
    for (const QPointF& f : layerQuadFrame()) qw << frameToWidget(f);
    const QPointF centre = frameToWidget(layerCentreFrame());
    const QPointF rot    = rotationHandleWidget(qw, centre);

    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#F0F0F0"));
    pen.setWidthF(1.2);

    // Bounding box + arm to the rotation handle
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(qw);
    p.drawLine((qw[0] + qw[1]) * 0.5, rot);

    // Corner (scale) handles — small filled squares
    p.setBrush(QColor("#F0F0F0"));
    const double h = 4.0;
    for (const QPointF& c : qw)
        p.drawRect(QRectF(c.x() - h, c.y() - h, 2 * h, 2 * h));

    // Rotation handle — round
    p.drawEllipse(rot, 4.5, 4.5);
}

void PreviewWidget::paintGroupHandles(QPainter& p)
{
    const QRectF gw = groupRectWidget();
    if (gw.isNull()) return;
    const QPointF topMid = (gw.topLeft() + gw.topRight()) * 0.5;
    const QPointF rot    = topMid + QPointF(0, -kRotArm);

    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#F0F0F0"));
    pen.setWidthF(1.2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(gw);
    p.drawLine(topMid, rot);

    p.setBrush(QColor("#F0F0F0"));
    const double h = 4.0;
    const QPointF corners[4] = { gw.topLeft(), gw.topRight(),
                                 gw.bottomRight(), gw.bottomLeft() };
    for (const QPointF& c : corners)
        p.drawRect(QRectF(c.x() - h, c.y() - h, 2 * h, 2 * h));
    p.drawEllipse(rot, 4.5, 4.5);
}

// Everything drawn ON TOP of the image: selection chrome, handles, loc dots,
// rubber band, status caption. Shared by the CPU paintEvent and (in GPU mode)
// the transparent overlay child, so both paths render identical chrome.
void PreviewWidget::paintOverlays(QPainter& p)
{
    if (!viewSizePx().isEmpty()) {
        if (!m_showOriginal && !m_panMode) {
            const bool single = handlesVisible();
            // Selection outlines for every selected layer (the single active one
            // is drawn with full handles instead).
            p.setRenderHint(QPainter::Antialiasing, true);
            QPen selPen(QColor("#568AD9"));
            selPen.setWidthF(1.4);
            p.setPen(selPen);
            p.setBrush(Qt::NoBrush);
            for (const CanvasLayer& cl : m_canvasLayers) {
                if (!m_selection.contains(cl.id)) continue;
                if (single && cl.id == m_activeId) continue;
                p.drawPolygon(quadWidget(cl.tf, cl.native));
            }
            if (single) paintHandles(p);
            else if (groupHandlesVisible()) paintGroupHandles(p);
            if (locShown()) paintLocHandles(p);
        }

        // Rubber-band box: dashed = crossing (drag left), solid = window (right).
        if (m_boxSelecting) {
            const QRect r = QRect(m_boxStart, m_boxCur).normalized();
            const bool crossing = m_boxCur.x() < m_boxStart.x();
            QPen boxPen(QColor("#E3E3E3"));
            boxPen.setWidthF(1.0);
            if (crossing) boxPen.setStyle(Qt::DashLine);
            p.setPen(boxPen);
            p.setBrush(QColor(227, 227, 227, 28));
            p.drawRect(r);
        }
    }

    // Status caption — bottom-left
    p.setPen(QColor("#828282"));
    QFont f = p.font();
    f.setPointSize(9);
    p.setFont(f);
    p.drawText(rect().adjusted(8, 0, -8, -6),
               Qt::AlignBottom | Qt::AlignLeft, m_status);
}

void PreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
    if (m_gpuActive) {
        // The canvas child draws the image; the overlay child draws the
        // chrome. Keep their geometry in lockstep and repaint the chrome.
        pushViewRect();
        if (m_overlay) m_overlay->update();
        QPainter p(this);
        p.fillRect(rect(), QColor("#1E1E1E"));   // WA_OpaquePaintEvent safety
        return;
    }

    QPainter p(this);
    p.fillRect(rect(), QColor("#1E1E1E"));

    if (!m_scaled.isNull()) {
        const int x = (width()  - m_scaled.width())  / 2 + m_panOffset.x();
        const int y = (height() - m_scaled.height()) / 2 + m_panOffset.y();

        // Checkerboard under transparent output
        if (m_scaled.hasAlphaChannel()) {
            const int cs = 8;
            const QRect imgRect(x, y, m_scaled.width(), m_scaled.height());
            for (int yy = 0; yy < imgRect.height(); yy += cs)
                for (int xx = 0; xx < imgRect.width(); xx += cs)
                    p.fillRect(x + xx, y + yy,
                               qMin(cs, imgRect.width() - xx),
                               qMin(cs, imgRect.height() - yy),
                               ((xx / cs + yy / cs) % 2) ? QColor("#2A2A2A") : QColor("#242424"));
        }

        p.drawImage(x, y, m_scaled);
    }

    paintOverlays(p);
}

void PreviewWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(kMediaMime)
     || !imagePathsFromMime(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void PreviewWidget::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasFormat(kMediaMime)) {
        emit mediaDroppedAsLayer(event->mimeData()->data(kMediaMime).toInt());
        event->acceptProposedAction();
        return;
    }
    const QStringList paths = imagePathsFromMime(event->mimeData());
    if (!paths.isEmpty()) emit filesDropped(paths);
}
