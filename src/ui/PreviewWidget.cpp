#include "PreviewWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
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

void PreviewWidget::setStatus(const QString& text)
{
    m_status = text;
    update();
}

void PreviewWidget::updateScaled()
{
    if (m_image.isNull()) { m_scaled = {}; return; }
    // Upscaling: nearest-neighbor preserves crisp symbol edges.
    // Downscaling: smooth avoids moiré on dense patterns.
    const bool upscaling = (m_image.width()  <= size().width()
                         && m_image.height() <= size().height());
    const auto mode = upscaling ? Qt::FastTransformation : Qt::SmoothTransformation;
    m_scaled = m_image.scaled(size(), Qt::KeepAspectRatio, mode);
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateScaled();
}

void PreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#1E1E1E"));

    if (!m_scaled.isNull()) {
        const int x = (width()  - m_scaled.width())  / 2;
        const int y = (height() - m_scaled.height()) / 2;

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
