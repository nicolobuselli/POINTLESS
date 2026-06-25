#include "PreviewWidget.h"
#include "Widgets.h"

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

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent)
    , m_status("Drop images here or use the orange button below")
{
    setAcceptDrops(true);
    setMinimumSize(300, 200);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void PreviewWidget::setImage(const QImage& img)
{
    m_image = img;
    updateScaled();
    update();
}

void PreviewWidget::setOriginalImage(const QImage& img)
{
    m_originalImage = img;
    if (m_showOriginal) {
        updateScaled();
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
    m_zoomFactor = 1.0;
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
    if (m_tfDrag != TfDrag::None) return;

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
    if (m_scaled.isNull() || m_frame.width() <= 0) return 0.0;
    return double(m_scaled.width()) / m_frame.width();
}

QPointF PreviewWidget::imageOrigin() const
{
    return QPointF((width()  - m_scaled.width())  / 2.0 + m_panOffset.x(),
                   (height() - m_scaled.height()) / 2.0 + m_panOffset.y());
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

bool PreviewWidget::handlesVisible() const
{
    return m_transformable && !m_panMode && !m_showOriginal
        && m_selection.size() == 1 && m_selection.contains(m_activeId);
}

const PreviewWidget::CanvasLayer* PreviewWidget::layerById(int id) const
{
    for (const CanvasLayer& cl : m_canvasLayers)
        if (cl.id == id) return &cl;
    return nullptr;
}

int PreviewWidget::hitTest(QPointF widgetPos) const
{
    // m_canvasLayers is top-first → first match is the top-most layer.
    for (const CanvasLayer& cl : m_canvasLayers)
        if (quadWidget(cl.tf, cl.native).containsPoint(widgetPos, Qt::OddEvenFill))
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
    updateScaled();
}

void PreviewWidget::wheelEvent(QWheelEvent* event)
{
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
    const bool leftPan = m_panMode && event->button() == Qt::LeftButton;
    const bool middlePan = event->button() == Qt::MiddleButton;
    if (!m_scaled.isNull() && (leftPan || middlePan)) {
        m_dragging = true;
        m_dragButton = event->button();
        m_lastDragPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // Selection + transform (left button, not in pan/original mode).
    if (event->button() == Qt::LeftButton && !m_panMode && !m_showOriginal
        && imageScale() > 0.0) {
        const QPointF pos = event->position();
        const Qt::KeyboardModifiers mods = event->modifiers();

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
    if (m_tfDrag != TfDrag::None) {
        const QPointF framePt = widgetToFrame(event->position());
        const QPointF c = layerCentreFrame();
        switch (m_tfDrag) {
            case TfDrag::Move: {
                const QPointF nc = framePt + m_grabOffset;
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
    QWidget::mouseMoveEvent(event);
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_tfDrag != TfDrag::None && event->button() == Qt::LeftButton) {
        m_tfDrag = TfDrag::None;
        setCursor(m_panMode ? Qt::OpenHandCursor : Qt::ArrowCursor);
        emit transformEditFinished();   // gesture over → MainWindow does a full render
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

void PreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
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
