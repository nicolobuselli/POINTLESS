#include "PreviewWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>

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
    m_zoomFactor = qBound(0.2, m_zoomFactor * factorStep, 8.0);
    updateScaled();
    update();

    setToolTip(QString("Zoom %1%").arg(int(m_zoomFactor * 100.0)));
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
    QWidget::mousePressEvent(event);
}

void PreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
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
    if (m_dragging && event->button() == m_dragButton) {
        m_dragging = false;
        m_dragButton = Qt::NoButton;
        setCursor(m_panMode ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
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
    if (!imagePathsFromMime(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void PreviewWidget::dropEvent(QDropEvent* event)
{
    const QStringList paths = imagePathsFromMime(event->mimeData());
    if (!paths.isEmpty()) emit filesDropped(paths);
}
