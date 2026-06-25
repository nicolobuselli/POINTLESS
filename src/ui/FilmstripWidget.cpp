#include "FilmstripWidget.h"
#include "Widgets.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QAbstractButton>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QMouseEvent>
#include <QDrag>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <functional>

namespace {

bool isImagePath(const QString& path)
{
    static const QStringList exts = { "png", "jpg", "jpeg", "bmp", "webp", "gif", "tif", "tiff" };
    return exts.contains(QFileInfo(path).suffix().toLower());
}

QStringList imagePathsFromMime(const QMimeData* mime)
{
    QStringList out;
    if (!mime || !mime->hasUrls()) return out;
    for (const QUrl& url : mime->urls()) {
        const QString p = url.toLocalFile();
        if (!p.isEmpty() && isImagePath(p)) out << p;
    }
    return out;
}

// ── Orange "add image" button (SVG asset) ────────────────────

class AddImageButton : public QAbstractButton {
public:
    explicit AddImageButton(QWidget* parent = nullptr) : QAbstractButton(parent) {
        setFixedSize(96, 116);
        setCursor(Qt::PointingHandCursor);
        setToolTip("Add images (or drop them anywhere)");
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        static QSvgRenderer renderer(QString(":/icons/add_image.svg"));

        // Fit the 147×186 artwork inside the button, centred,
        // with a slight grow on hover.
        const QSizeF art(147.0, 186.0);
        const qreal pad   = underMouse() ? 4.0 : 8.0;
        const QRectF area = QRectF(rect()).adjusted(pad, pad, -pad, -pad);
        QSizeF s = art.scaled(area.size(), Qt::KeepAspectRatio);
        QRectF target(QPointF(area.center().x() - s.width()  / 2.0,
                              area.center().y() - s.height() / 2.0), s);
        renderer.render(&p, target);
    }
};

} // namespace

// ── FilmstripThumb ───────────────────────────────────────────

class FilmstripThumb : public QWidget {
public:
    std::function<void()> onClicked;     // single click → select
    std::function<void()> onActivated;   // double click → add as layer
    std::function<void()> onClose;
    std::function<void(FilmstripThumb*)> onDragStart;   // → start a media drag

    int  mediaId() const { return m_mediaId; }

    FilmstripThumb(int mediaId, const QImage& source, const QString& name, QWidget* parent = nullptr)
        : QWidget(parent), m_mediaId(mediaId), m_name(name)
    {
        setFixedSize(168, 100);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setToolTip(name);

        // Cover-scaled pixmap
        QImage scaled = source.scaled(size() * 2, Qt::KeepAspectRatioByExpanding,
                                      Qt::SmoothTransformation);
        QImage cropped(size() * 2, QImage::Format_ARGB32_Premultiplied);
        cropped.fill(Qt::transparent);
        {
            QPainter cp(&cropped);
            cp.drawImage(QPoint((cropped.width()  - scaled.width())  / 2,
                                (cropped.height() - scaled.height()) / 2), scaled);
        }
        m_pixmap = QPixmap::fromImage(cropped);
        m_pixmap.setDevicePixelRatio(2.0);
    }

    void setActive(bool active) { m_active = active; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QPainterPath clip;
        clip.addRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
        p.save();
        p.setClipPath(clip);
        p.fillRect(rect(), QColor("#3B3B3B"));
        p.drawPixmap(1, 1, m_pixmap);
        p.restore();

        if (m_active) {
            p.setPen(QPen(QColor("#F95B1E"), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
        }

        if (m_hover) {
            const QRectF cr = closeRect();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 150));
            p.drawEllipse(cr);
            QPen xp(Qt::white, 1.6);
            xp.setCapStyle(Qt::RoundCap);
            p.setPen(xp);
            const qreal m = 4.5;
            p.drawLine(cr.topLeft()    + QPointF(m, m), cr.bottomRight() - QPointF(m, m));
            p.drawLine(cr.topRight()   + QPointF(-m, m), cr.bottomLeft() + QPointF(m, -m));
        }
    }

    void enterEvent(QEnterEvent*) override { m_hover = true; update(); }
    void leaveEvent(QEvent*) override      { m_hover = false; update(); }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        if (m_hover && closeRect().contains(e->position())) {
            m_pressOnClose = true;
            return;
        }
        m_pressOnClose = false;
        m_pressPos = e->position().toPoint();
        m_dragging = false;
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_pressOnClose || m_dragging || !(e->buttons() & Qt::LeftButton)) return;
        if ((e->position().toPoint() - m_pressPos).manhattanLength()
                < QApplication::startDragDistance())
            return;
        m_dragging = true;
        if (onDragStart) onDragStart(this);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        if (m_pressOnClose) {
            if (m_hover && closeRect().contains(e->position()) && onClose) onClose();
        } else if (!m_dragging) {
            if (onClicked) onClicked();
        }
        m_dragging = false;
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && onActivated) onActivated();
    }

private:
    QRectF closeRect() const { return QRectF(width() - 24, 6, 18, 18); }

    int     m_mediaId = -1;
    QString m_name;
    QPixmap m_pixmap;
    bool    m_active = false;
    bool    m_hover  = false;
    QPoint  m_pressPos;
    bool    m_pressOnClose = false;
    bool    m_dragging     = false;
};

// ── FilmstripWidget ──────────────────────────────────────────

FilmstripWidget::FilmstripWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("filmstrip");
    setFixedHeight(140);
    setAcceptDrops(true);

    auto* hl = new QHBoxLayout(this);
    hl->setContentsMargins(16, 10, 10, 10);
    hl->setSpacing(10);

    auto* addBtn = new AddImageButton;
    connect(addBtn, &QAbstractButton::clicked, this, &FilmstripWidget::addRequested);
    hl->addWidget(addBtn);

    m_leftBtn = new ChevronButton(ChevronButton::Left);
    connect(m_leftBtn, &QPushButton::clicked, this, [this]() { scrollBy(-360); });
    hl->addWidget(m_leftBtn);

    m_scroll = new QScrollArea;
    m_scroll->setObjectName("filmstripScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_thumbRow = new QWidget;
    m_thumbRow->setObjectName("filmstripRow");
    m_thumbLayout = new QHBoxLayout(m_thumbRow);
    m_thumbLayout->setContentsMargins(0, 0, 0, 0);
    m_thumbLayout->setSpacing(10);
    m_thumbLayout->addStretch();

    m_scroll->setWidget(m_thumbRow);
    hl->addWidget(m_scroll, 1);

    m_rightBtn = new ChevronButton(ChevronButton::Right);
    connect(m_rightBtn, &QPushButton::clicked, this, [this]() { scrollBy(360); });
    hl->addWidget(m_rightBtn);
}

void FilmstripWidget::addThumb(int mediaId, const QImage& source, const QString& name)
{
    auto* thumb = new FilmstripThumb(mediaId, source, name);
    m_thumbs.append(thumb);
    // Insert before the trailing stretch
    m_thumbLayout->insertWidget(m_thumbLayout->count() - 1, thumb);

    thumb->onClicked   = [this, mediaId]() { emit thumbSelected(mediaId); };
    thumb->onActivated = [this, mediaId]() { emit thumbActivated(mediaId); };
    thumb->onClose     = [this, mediaId]() { emit thumbCloseRequested(mediaId); };
    thumb->onDragStart = [mediaId](FilmstripThumb* t) {
        auto* mime = new QMimeData;
        mime->setData(kMediaMime, QByteArray::number(mediaId));
        auto* drag = new QDrag(t);
        drag->setMimeData(mime);
        drag->setPixmap(t->grab().scaled(96, 57, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
        drag->exec(Qt::CopyAction);
    };
}

void FilmstripWidget::removeThumb(int mediaId)
{
    for (int i = 0; i < m_thumbs.size(); ++i) {
        if (m_thumbs[i]->mediaId() != mediaId) continue;
        FilmstripThumb* t = m_thumbs.takeAt(i);
        m_thumbLayout->removeWidget(t);
        t->deleteLater();
        return;
    }
}

void FilmstripWidget::setActive(int mediaId)
{
    for (FilmstripThumb* t : m_thumbs)
        t->setActive(t->mediaId() == mediaId);
}

void FilmstripWidget::scrollBy(int dx)
{
    auto* bar = m_scroll->horizontalScrollBar();
    bar->setValue(bar->value() + dx);
}

void FilmstripWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (!imagePathsFromMime(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void FilmstripWidget::dropEvent(QDropEvent* event)
{
    const QStringList paths = imagePathsFromMime(event->mimeData());
    if (!paths.isEmpty()) emit filesDropped(paths);
}
