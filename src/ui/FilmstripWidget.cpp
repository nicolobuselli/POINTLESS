#include "FilmstripWidget.h"
#include "Widgets.h"
#include "UiScale.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QAbstractButton>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QMouseEvent>
#include <QResizeEvent>
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

} // namespace

// ── "Import" button (import.svg) ─────────────────────────────
// Square, resized by the grid (setSquareSize) to match a thumbnail cell —
// same convention as FilmstripThumb — so it reads as the same height as the
// row of images beside it. The artwork is itself square, so it fills the
// whole cell (just a hairline of breathing room), not a small inset icon.

class AddImageButton : public QAbstractButton {
public:
    explicit AddImageButton(QWidget* parent = nullptr) : QAbstractButton(parent) {
        setFixedSize(Ui::px(96), Ui::px(96));
        setCursor(Qt::PointingHandCursor);
        setToolTip("Import images");
    }

    void setSquareSize(int px) {
        px = qMax(1, px);
        if (width() == px && height() == px) return;
        setFixedSize(px, px);
        update();
    }

protected:
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override       { update(); }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        static QSvgRenderer renderer(QString(":/icons/import.svg"));

        const qreal pad   = underMouse() ? 3.0 : 2.0;
        const QRectF area = QRectF(rect()).adjusted(pad, pad, -pad, -pad);
        QSizeF s = QSizeF(377.0, 371.0).scaled(area.size(), Qt::KeepAspectRatio);
        QRectF target(QPointF(area.center().x() - s.width()  / 2.0,
                              area.center().y() - s.height() / 2.0), s);
        renderer.render(&p, target);
    }
};

// ── Empty-state "import" CTA button (cta_import.svg) ─────────
// Shown only when the library is empty, in place of everything else — the
// scalloped pill from cta_import.svg as the button chrome, "import" text
// on top in the same lime the shape's own bevel uses.

class ImportCTAButton : public QAbstractButton {
public:
    explicit ImportCTAButton(QWidget* parent = nullptr) : QAbstractButton(parent) {
        setFixedSize(Ui::px(145), qRound(Ui::px(145) * 118.0 / 349.0));
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override       { update(); }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        // Hover swaps in a variant with the bevel/stroke path lightened —
        // same "stroke brightens on hover" language as the standard box
        // system (@boxStroke → @boxStrokeHover).
        static QSvgRenderer renderer(QString(":/icons/cta_import.svg"));
        static QSvgRenderer hoverRenderer(QString(":/icons/cta_import_hover.svg"));
        (underMouse() ? hoverRenderer : renderer).render(&p, QRectF(rect()));

        QFont f = p.font();
        f.setPixelSize(Ui::px(15));
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor("#D2FC51"));
        p.drawText(rect(), Qt::AlignCenter, "import");
    }
};

// ── FilmstripThumb ───────────────────────────────────────────
// Square library cell: cover-crops whatever aspect ratio the source image
// has to fill the cell exactly. Size is set by the grid (setSquareSize),
// not fixed at construction, so the whole grid can reflow when the column
// stays 6-wide but the available width changes.

class FilmstripThumb : public QWidget {
public:
    std::function<void()> onClicked;     // single click → select
    std::function<void()> onActivated;   // double click → add as layer
    std::function<void()> onClose;
    std::function<void(FilmstripThumb*)> onDragStart;   // → start a media drag

    int  mediaId() const { return m_mediaId; }

    FilmstripThumb(int mediaId, const QImage& source, const QString& name, QWidget* parent = nullptr)
        : QWidget(parent), m_mediaId(mediaId), m_name(name), m_source(source)
    {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setToolTip(name);
    }

    void setActive(bool active) { m_active = active; update(); }

    // Square cell of side `px`: re-crops the source image to fill it exactly
    // (cover, not letterboxed) regardless of the source's own aspect ratio.
    void setSquareSize(int px)
    {
        px = qMax(1, px);
        if (width() == px && height() == px && !m_pixmap.isNull()) return;
        setFixedSize(px, px);
        rebuildPixmap();
    }

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
            p.setPen(QPen(QColor("#3D3D3D"), 1));   // @boxStroke — same gray as every normal box
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

    void rebuildPixmap()
    {
        // Cover-scaled pixmap: fills the square cell, cropping whichever
        // dimension overhangs, regardless of the source's own aspect ratio.
        QImage scaled = m_source.scaled(size() * 2, Qt::KeepAspectRatioByExpanding,
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
        update();
    }

    int     m_mediaId = -1;
    QString m_name;
    QImage  m_source;
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
    setAcceptDrops(true);

    auto* hl = new QHBoxLayout(this);
    hl->setContentsMargins(Ui::px(12), Ui::px(6), Ui::px(8), Ui::px(6));
    hl->setSpacing(Ui::px(10));

    m_addBtn = new AddImageButton;
    connect(m_addBtn, &QAbstractButton::clicked, this, &FilmstripWidget::addRequested);
    hl->addWidget(m_addBtn, 0, Qt::AlignTop);
    // Extra gap (on top of the regular inter-cell spacing) so the import
    // tile doesn't read as just another thumbnail in the grid.
    hl->addSpacing(Ui::px(kAddGapExtraFigmaPx));

    // Square grid, vertically scrollable — starts right where the add button
    // ends and stretches to the panel's right edge (the same span the
    // chevrons + horizontal strip used to occupy). Past the column count it
    // wraps to a new row; once rows overflow the visible height the overlay
    // scrollbar appears instead of clipping.
    m_scroll = new QScrollArea;
    m_scroll->setObjectName("filmstripScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_thumbRow = new QWidget;
    m_thumbRow->setObjectName("filmstripRow");
    m_thumbLayout = new QGridLayout(m_thumbRow);
    m_thumbLayout->setContentsMargins(0, 0, 0, 0);
    m_thumbLayout->setSpacing(Ui::px(10));
    // setWidgetResizable(true) stretches m_thumbRow to the viewport's full
    // height; without this, a grid shorter than that gets vertically
    // centred inside it (Qt's default for a Fixed-size cell's leftover
    // row space) instead of staying pinned to the top, next to the add
    // button.
    m_thumbLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_thumbRow->installEventFilter(this);

    m_scroll->setWidget(m_thumbRow);
    installOverlayScrollbar(m_scroll);
    hl->addWidget(m_scroll, 1);

    // Shown only while the library is empty, in place of the add button and
    // the grid — not overlaid on top of them (a bare drop hint used to share
    // space with the "+" icon; now nothing else should be visible at all).
    // Parented to `this` and kept in sync with this widget's own size in
    // resizeEvent below.
    m_emptyState = new QWidget(this);
    auto* evl = new QVBoxLayout(m_emptyState);
    evl->setContentsMargins(Ui::px(20), Ui::px(24), Ui::px(20), 0);
    auto* label = new QLabel("let's ruin a perfectly good image");
    label->setObjectName("filmstripEmptyHint");
    label->setAlignment(Qt::AlignCenter);
    evl->addWidget(label, 0, Qt::AlignHCenter);
    evl->addSpacing(Ui::px(16));
    auto* cta = new ImportCTAButton;
    connect(cta, &QAbstractButton::clicked, this, &FilmstripWidget::addRequested);
    evl->addWidget(cta, 0, Qt::AlignHCenter);
    evl->addStretch(1);   // sticky to the top — only the trailing space stretches

    m_addBtn->hide();
    m_scroll->hide();
    m_emptyState->raise();
}

void FilmstripWidget::addThumb(int mediaId, const QImage& source, const QString& name)
{
    auto* thumb = new FilmstripThumb(mediaId, source, name, m_thumbRow);
    m_thumbs.append(thumb);

    thumb->onClicked   = [this, mediaId]() { emit thumbSelected(mediaId); };
    thumb->onActivated = [this, mediaId]() { emit thumbActivated(mediaId); };
    thumb->onClose     = [this, mediaId]() { emit thumbCloseRequested(mediaId); };
    thumb->onDragStart = [mediaId](FilmstripThumb* t) {
        auto* mime = new QMimeData;
        mime->setData(kMediaMime, QByteArray::number(mediaId));
        auto* drag = new QDrag(t);
        drag->setMimeData(mime);
        drag->setPixmap(t->grab().scaled(96, 96, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
        drag->exec(Qt::CopyAction);
    };

    if (m_thumbs.size() == 1) {
        m_emptyState->hide();
        m_addBtn->show();
        m_scroll->show();
    }
    relayoutGrid();
}

void FilmstripWidget::removeThumb(int mediaId)
{
    for (int i = 0; i < m_thumbs.size(); ++i) {
        if (m_thumbs[i]->mediaId() != mediaId) continue;
        FilmstripThumb* t = m_thumbs.takeAt(i);
        m_thumbLayout->removeWidget(t);
        t->deleteLater();
        if (m_thumbs.isEmpty()) {
            m_addBtn->hide();
            m_scroll->hide();
            m_emptyState->show();
        }
        relayoutGrid();
        return;
    }
}

void FilmstripWidget::clear()
{
    for (FilmstripThumb* t : m_thumbs) {
        m_thumbLayout->removeWidget(t);
        t->deleteLater();
    }
    m_thumbs.clear();
    m_addBtn->hide();
    m_scroll->hide();
    m_emptyState->show();
    relayoutGrid();
}

void FilmstripWidget::setActive(int mediaId)
{
    for (FilmstripThumb* t : m_thumbs)
        t->setActive(t->mediaId() == mediaId);
}

// Both this and applyCellSizesOnly() size off FilmstripWidget's own width,
// not m_thumbRow's (the grid viewport): the add button and the grid are
// siblings sharing one row, so resizing the add button changes the grid's
// available width — reading that back would feed into the next cell-size
// computation and could settle on a stale, mismatched size (add button and
// thumbnails computed from two different widths a frame apart). This
// widget's own width only changes from an actual outer resize, so deriving
// everything from it keeps the add button and every thumbnail cell exactly
// equal, in one pass, every time.
int FilmstripWidget::hMargins() const {
    return Ui::px(16) + Ui::px(10) + Ui::px(kAddGapExtraFigmaPx);   // hl's own left+right content margins + the import tile's extra gap
}

// Columns grow past kMinColumns once the viewport is wide enough that
// kMinColumns squares would each exceed kMaxCellFigmaPx — so a big monitor
// gets more, same-sized thumbnails instead of a few giant ones.
int FilmstripWidget::computeColumns() const
{
    const int spacing = Ui::px(10);
    const int maxCell = Ui::px(kMaxCellFigmaPx);
    const int avail   = width() - hMargins();
    const int denom   = qMax(1, maxCell + spacing);
    const int nSlots  = (avail + spacing + denom - 1) / denom;   // ceil division
    return qMax(kMinColumns, nSlots - 1);   // -1: one slot is the add button, not a thumbnail column
}

// Re-seats every thumb at its row/col (order = insertion order) at the
// current column count, then resizes them all to fill the row. Called
// whenever the thumb list changes, or applyCellSizes() finds the column
// count itself needs to change.
void FilmstripWidget::relayoutGrid()
{
    m_columns = computeColumns();
    for (int c = 0; c < 24; ++c) m_thumbLayout->setColumnStretch(c, c < m_columns ? 1 : 0);
    for (int i = 0; i < m_thumbs.size(); ++i) {
        m_thumbLayout->removeWidget(m_thumbs[i]);
        m_thumbLayout->addWidget(m_thumbs[i], i / m_columns, i % m_columns);
    }
    applyCellSizesOnly();
}

// Resize-only, for a plain viewport-width change: cheap common case. Falls
// back to a full relayoutGrid() if the width crossed a column-count threshold.
void FilmstripWidget::applyCellSizes()
{
    if (computeColumns() != m_columns) { relayoutGrid(); return; }
    applyCellSizesOnly();
}

void FilmstripWidget::applyCellSizesOnly()
{
    const int spacing = Ui::px(10);
    const int avail   = width() - hMargins();
    const int nSlots  = m_columns + 1;   // add button + one per thumbnail column
    const int cell    = qMax(1, (avail - (nSlots - 1) * spacing) / nSlots);
    for (FilmstripThumb* t : m_thumbs) t->setSquareSize(cell);
    if (m_addBtn) m_addBtn->setSquareSize(cell);
}

void FilmstripWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    applyCellSizes();
    m_emptyState->setGeometry(rect());
}

bool FilmstripWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_thumbRow && event->type() == QEvent::Resize) {
        applyCellSizes();
    }
    return QWidget::eventFilter(obj, event);
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
