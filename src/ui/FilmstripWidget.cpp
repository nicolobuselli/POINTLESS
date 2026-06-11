#include "FilmstripWidget.h"
#include "Widgets.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QAbstractButton>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
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

// ── Orange tilted "add" button ───────────────────────────────

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

        const QPointF c(width() / 2.0, height() / 2.0);
        const qreal w = 54, h = 72, r = 10;

        // Back card
        p.save();
        p.translate(c);
        p.rotate(-9);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#B8430F"));
        p.drawRoundedRect(QRectF(-w/2 - 4, -h/2 + 3, w, h), r, r);
        p.restore();

        // Front card
        p.save();
        p.translate(c);
        p.rotate(7);
        p.setPen(Qt::NoPen);
        p.setBrush(underMouse() ? QColor("#FF6E33") : QColor("#F95B1E"));
        p.drawRoundedRect(QRectF(-w/2, -h/2, w, h), r, r);

        // Plus sign
        QPen pen(Qt::white, 4);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        const qreal s = 11;
        p.drawLine(QPointF(-s, 0), QPointF(s, 0));
        p.drawLine(QPointF(0, -s), QPointF(0, s));
        p.restore();
    }
};

} // namespace

// ── FilmstripThumb ───────────────────────────────────────────

class FilmstripThumb : public QWidget {
public:
    std::function<void()> onClicked;
    std::function<void()> onClose;

    FilmstripThumb(const QImage& source, const QString& name, QWidget* parent = nullptr)
        : QWidget(parent), m_name(name)
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
            if (onClose) onClose();
        } else {
            if (onClicked) onClicked();
        }
    }

private:
    QRectF closeRect() const { return QRectF(width() - 24, 6, 18, 18); }

    QString m_name;
    QPixmap m_pixmap;
    bool m_active = false;
    bool m_hover  = false;
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

int FilmstripWidget::addThumb(const QImage& source, const QString& name)
{
    auto* thumb = new FilmstripThumb(source, name);
    m_thumbs.append(thumb);
    // Insert before the trailing stretch
    m_thumbLayout->insertWidget(m_thumbLayout->count() - 1, thumb);

    thumb->onClicked = [this, thumb]() {
        int idx = m_thumbs.indexOf(thumb);
        if (idx >= 0) emit thumbSelected(idx);
    };
    thumb->onClose = [this, thumb]() {
        int idx = m_thumbs.indexOf(thumb);
        if (idx >= 0) emit thumbCloseRequested(idx);
    };
    return m_thumbs.size() - 1;
}

void FilmstripWidget::removeThumb(int index)
{
    if (index < 0 || index >= m_thumbs.size()) return;
    FilmstripThumb* t = m_thumbs.takeAt(index);
    m_thumbLayout->removeWidget(t);
    t->deleteLater();
}

void FilmstripWidget::setActive(int index)
{
    for (int i = 0; i < m_thumbs.size(); ++i)
        m_thumbs[i]->setActive(i == index);
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
