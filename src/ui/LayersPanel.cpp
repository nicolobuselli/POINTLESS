#include "LayersPanel.h"
#include "Widgets.h"
#include "UiScale.h"
#include "../core/ImageAdjuster.h"
#include "../workers/RenderWorker.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QLineEdit>
#include <QMenu>
#include <QContextMenuEvent>

namespace {

constexpr int  kPanelWidth = 240;
constexpr int  kMargin     = 12;
const char*    kLayerMime  = "application/x-ultraditherer-layer";
const char*    kParentMime = "application/x-ultraditherer-parent";

void repolish(QWidget* w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

QPixmap roundedThumb(const QImage& source, const QSize& size, qreal radius,
                      const QColor& background = QColor("#3B3B3B"), float opacity = 1.0f)
{
    QPixmap out(size);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), radius, radius);
    p.setClipPath(clip);

    QColor bg = background;
    if (!bg.isValid()) bg = QColor("#3B3B3B");
    bg.setAlphaF(opacity);

    p.fillRect(out.rect(), bg);
    if (!source.isNull()) {
        const QImage scaled = source.scaled(size, Qt::KeepAspectRatioByExpanding,
                                            Qt::SmoothTransformation);
        p.drawImage(QPoint((size.width()  - scaled.width())  / 2,
                           (size.height() - scaled.height()) / 2), scaled);
    }
    p.end();
    return out;
}

// Blend modes in Photoshop menu order, grouped as in the menu.
struct BlendEntry { BlendMode mode; const char* name; bool groupStart; };
const BlendEntry kBlendEntries[] = {
    { BlendMode::Normal,       "Normal",             false },
    { BlendMode::Dissolve,     "Dissolve",           false },
    { BlendMode::Darken,       "Darken",             true  },
    { BlendMode::Multiply,     "Multiply",           false },
    { BlendMode::ColorBurn,    "Color Burn",         false },
    { BlendMode::LinearBurn,   "Linear Burn",        false },
    { BlendMode::DarkerColor,  "Darker Color",       false },
    { BlendMode::Lighten,      "Lighten",            true  },
    { BlendMode::Screen,       "Screen",             false },
    { BlendMode::ColorDodge,   "Color Dodge",        false },
    { BlendMode::LinearDodge,  "Linear Dodge (Add)", false },
    { BlendMode::LighterColor, "Lighter Color",      false },
    { BlendMode::Overlay,      "Overlay",            true  },
    { BlendMode::SoftLight,    "Soft Light",         false },
    { BlendMode::HardLight,    "Hard Light",         false },
    { BlendMode::VividLight,   "Vivid Light",        false },
    { BlendMode::LinearLight,  "Linear Light",       false },
    { BlendMode::PinLight,     "Pin Light",          false },
    { BlendMode::HardMix,      "Hard Mix",           false },
    { BlendMode::Difference,   "Difference",         true  },
    { BlendMode::Exclusion,    "Exclusion",          false },
    { BlendMode::Subtract,     "Subtract",           false },
    { BlendMode::Divide,       "Divide",             false },
    { BlendMode::Hue,          "Hue",                true  },
    { BlendMode::Saturation,   "Saturation",         false },
    { BlendMode::Color,        "Color",              false },
    { BlendMode::Luminosity,   "Luminosity",         false },
};

} // namespace

// ============================================================
//  LayerRow — one entry in the stack
// ============================================================

class LayerRow : public QFrame
{
public:
    std::function<void()>            onSelected;
    std::function<void()>            onShiftSelected;   // range-select to anchor
    std::function<void()>            onCtrlSelected;    // toggle this row in/out
    std::function<void(bool)>       onEyeToggled;
    std::function<void(bool)>       onLockToggled;
    std::function<void(const QString&)> onRenamed;
    std::function<void()>            onDeleteRequested;
    std::function<void()>            onRemoveEditsRequested;
    std::function<void()>            onCopyRequested;
    std::function<void()>            onPasteRequested;

    void setDeletable(bool d) { m_deletable = d; }
    void setHasEdits(bool h)  { m_hasEdits = h; }

    // Indent a child row so the tree connector has room on its left.
    void setIndent(int leftPx)
    {
        if (auto* l = qobject_cast<QHBoxLayout*>(layout()))
            l->setContentsMargins(leftPx, Ui::px(2), 0, Ui::px(2));
    }

    LayerRow(int layerId, QWidget* parent = nullptr)
        : QFrame(parent), m_id(layerId)
    {
        // Outer row is transparent; the coloured selection "pill" wraps only
        // the thumbnail + name, while the eye sits in the gutter to its right.
        setObjectName("layerRowOuter");
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(Ui::px(52));

        auto* hl = new QHBoxLayout(this);
        hl->setContentsMargins(0, Ui::px(2), 0, Ui::px(2));
        hl->setSpacing(0);

        m_pill = new QFrame(this);
        m_pill->setObjectName("layerRow");
        m_pill->setProperty("selected", false);
        m_pill->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* pl = new QHBoxLayout(m_pill);
        pl->setContentsMargins(Ui::px(8), Ui::px(4), Ui::px(12), Ui::px(4));
        pl->setSpacing(Ui::px(10));

        m_thumb = new QLabel;
        m_thumb->setFixedSize(Ui::px(46), Ui::px(32));
        m_thumb->setStyleSheet("background: transparent;");
        m_thumb->setAttribute(Qt::WA_TransparentForMouseEvents);
        pl->addWidget(m_thumb);

        m_name = new QLabel;
        m_name->setObjectName("layerName");
        m_name->setTextInteractionFlags(Qt::NoTextInteraction);
        m_name->setAttribute(Qt::WA_TransparentForMouseEvents);
        pl->addWidget(m_name, 1);

        // Inline rename editor shares the name's layout slot (one is hidden at a
        // time), so it never overlaps the thumbnail. Transparent + borderless +
        // label-sized so it blends into the row (the generic QLineEdit rule's
        // border + 48px min-height would otherwise clip it).
        m_nameEdit = new QLineEdit;
        m_nameEdit->setObjectName("layerNameEdit");
        m_nameEdit->setFrame(false);
        m_nameEdit->setVisible(false);
        m_nameEdit->setStyleSheet(QString(
            "background:transparent; border:none; padding:0; margin:0; min-height:0;"
            " color:#FFFFFF; font-size:%1px; font-weight:400;"
            " selection-background-color:#FD5A1F;").arg(Ui::px(18)));
        pl->addWidget(m_nameEdit, 1);

        hl->addWidget(m_pill, 1);

        connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
            finishNameEditing();
        });

        m_eye = new QPushButton;
        m_eye->setObjectName("eyeBtn");
        m_eye->setFixedSize(Ui::px(30), Ui::px(30));
        m_eye->setCursor(Qt::PointingHandCursor);
        m_eye->setIconSize(QSize(Ui::px(20), Ui::px(20)));

        // The eye lives centred in the right gutter (≈70px), detached from
        // the pill, lined up with the parameters' icon gutter.
        auto* eyeWrap = new QWidget(this);
        eyeWrap->setFixedWidth(Ui::px(70));
        auto* ew = new QHBoxLayout(eyeWrap);
        ew->setContentsMargins(0, 0, 0, 0);
        ew->addWidget(m_eye, 0, Qt::AlignCenter);
        hl->addWidget(eyeWrap);

        connect(m_eye, &QPushButton::clicked, this, [this]() {
            setLayerVisible(!m_visible);
            if (onEyeToggled) onEyeToggled(m_visible);
        });

        // Padlock inside the pill, on its right: appears on row hover (or
        // always when locked). The pill is WA_TransparentForMouseEvents —
        // that swallows its children's clicks too — so the lock is parented
        // to the row and overlaid on the pill in resizeEvent.
        m_lock = new QPushButton(this);
        m_lock->setObjectName("eyeBtn");   // same transparent icon chrome
        m_lock->setFixedSize(Ui::px(30), Ui::px(30));
        m_lock->setCursor(Qt::PointingHandCursor);
        m_lock->setIconSize(QSize(Ui::px(18), Ui::px(18)));
        m_lock->setToolTip("Lock layer (canvas clicks ignore it; panels still edit it)");
        m_lock->hide();
        connect(m_lock, &QPushButton::clicked, this, [this]() {
            setLocked(!m_locked);
            if (onLockToggled) onLockToggled(m_locked);
        });
    }

    int layerId() const { return m_id; }

    void setThumb(const QPixmap& px) { m_thumb->setPixmap(px); }
    void setName(const QString& n)   { m_name->setText(n); }

    void setLayerVisible(bool on)
    {
        m_visible = on;
        m_eye->setIcon(QIcon(on ? ":/icons/eye_open.svg" : ":/icons/eye_closed.svg"));
    }

    void setLocked(bool on)
    {
        m_locked = on;
        m_lock->setIcon(QIcon(on ? ":/icons/lock.svg" : ":/icons/lock_open.svg"));
        m_lock->setVisible(on || m_hovered);
    }

    void setSelected(bool sel)
    {
        if (m_pill->property("selected").toBool() == sel) return;
        m_pill->setProperty("selected", sel);
        repolish(m_pill);
        repolish(m_name);
    }

protected:
    void enterEvent(QEnterEvent* e) override
    {
        m_hovered = true;
        m_lock->setVisible(true);
        QFrame::enterEvent(e);
    }

    void leaveEvent(QEvent* e) override
    {
        m_hovered = false;
        m_lock->setVisible(m_locked);   // stays up while locked
        QFrame::leaveEvent(e);
    }

    void resizeEvent(QResizeEvent* e) override
    {
        QFrame::resizeEvent(e);
        // Overlay the lock on the pill's right edge (pill = row minus the
        // 70px eye gutter), vertically centred.
        const int x = width() - Ui::px(70) - Ui::px(12) - m_lock->width();
        m_lock->move(qMax(0, x), (height() - m_lock->height()) / 2);
        m_lock->raise();
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && !m_editingName) {
            m_pressPos = e->pos();
            if ((e->modifiers() & Qt::ShiftModifier) && onShiftSelected) onShiftSelected();
            else if ((e->modifiers() & Qt::ControlModifier) && onCtrlSelected) onCtrlSelected();
            else if (onSelected) onSelected();
        }
        QFrame::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_editingName) return;
        if (!(e->buttons() & Qt::LeftButton)) return;
        if ((e->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance())
            return;

        auto* mime = new QMimeData;
        mime->setData(kLayerMime, QByteArray::number(m_id));

        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(grab());
        drag->setHotSpot(m_pressPos);
        drag->exec(Qt::MoveAction);
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_pill->geometry().contains(e->pos())) {
            beginNameEditing();
            e->accept();
            return;
        }
        QFrame::mouseDoubleClickEvent(e);
    }

    void contextMenuEvent(QContextMenuEvent* e) override
    {
        QMenu menu(this);
        QAction* copy  = menu.addAction("Copy layer");
        QAction* paste = menu.addAction("Paste layer");
        menu.addSeparator();
        QAction* del     = m_deletable ? menu.addAction("Delete layer")  : nullptr;
        QAction* rmEdits = m_hasEdits  ? menu.addAction("Remove edits") : nullptr;
        QAction* chosen = menu.exec(e->globalPos());
        if (chosen == copy && onCopyRequested)
            onCopyRequested();
        else if (chosen == paste && onPasteRequested)
            onPasteRequested();
        else if (chosen && chosen == del && onDeleteRequested)
            onDeleteRequested();
        else if (chosen && chosen == rmEdits && onRemoveEditsRequested)
            onRemoveEditsRequested();
    }

private:
    void beginNameEditing()
    {
        if (m_editingName) return;
        m_editingName = true;
        m_nameEdit->setText(m_name->text());
        m_name->hide();
        m_nameEdit->show();
        m_nameEdit->setFocus(Qt::MouseFocusReason);
        m_nameEdit->selectAll();
    }

    void finishNameEditing()
    {
        if (!m_editingName) return;
        m_editingName = false;
        const QString text = m_nameEdit->text().trimmed();
        if (!text.isEmpty() && text != m_name->text()) {
            setName(text);
            if (onRenamed) onRenamed(text);
        }
        m_nameEdit->hide();
        m_name->show();
    }

    void cancelNameEditing()
    {
        if (!m_editingName) return;
        m_editingName = false;
        m_nameEdit->hide();
        m_name->show();
    }

    int          m_id;
    bool         m_visible = true;
    bool         m_locked  = false;
    bool         m_hovered = false;
    bool         m_editingName = false;
    bool         m_deletable = true;
    bool         m_hasEdits  = false;
    QFrame*      m_pill    = nullptr;
    QLabel*      m_thumb   = nullptr;
    QLabel*      m_name    = nullptr;
    QLineEdit*   m_nameEdit = nullptr;
    QPushButton* m_eye     = nullptr;
    QPushButton* m_lock    = nullptr;
    QPoint       m_pressPos;
};

// ============================================================
//  RowsArea — hosts the rows; accepts drops for reordering
// ============================================================

// Tree indent / connector geometry (Figma px).
namespace tree {
    inline int indent()   { return Ui::px(30); }   // child left inset
    inline int trunkX()   { return Ui::px(14); }   // vertical connector x
}

class RowsArea : public QWidget
{
public:
    // One visible row, in top→bottom order, with the metadata the area needs to
    // draw connectors and resolve drops.
    struct Row {
        QWidget* w             = nullptr;
        bool     isParent      = false;
        int      mediaId       = -1;   // parent: own; child: its parent's
        int      layerId       = -1;   // child only
    };

    std::function<void(int, int)> onChildReorder;    // layerId, insertIndex
    std::function<void(int, int)> onChildDuplicate;  // layerId, insertIndex (Alt+drag)
    std::function<void(int, int)> onParentReorder;   // mediaId, insertIndex
    std::function<void(int)>      onAddChild;         // mediaId
    std::function<void(int)>      onMediaDropped;     // library source → new layer

    explicit RowsArea(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAcceptDrops(true);
        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(Ui::px(6));
    }

    QVBoxLayout* rowsLayout() const { return m_layout; }
    void setModel(const QVector<Row>& rows) { m_model = rows; update(); }
    void clearModel() { m_model.clear(); }

protected:
    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasFormat(kLayerMime) || e->mimeData()->hasFormat(kParentMime)
         || e->mimeData()->hasFormat(kMediaMime))
            e->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent* e) override
    {
        const QPoint pos = e->position().toPoint();
        if (e->mimeData()->hasFormat(kLayerMime)) {
            m_addChildMedia = -1;
            m_indicatorY = childIndicatorY(childInsertIndexAt(pos));
        } else if (e->mimeData()->hasFormat(kParentMime)) {
            const int hoverMedia = childZoneMediaAt(pos);
            if (hoverMedia >= 0) {            // over a group's children area → add child
                m_addChildMedia = hoverMedia;
                m_indicatorY = -1;
            } else {                          // between groups → reorder
                m_addChildMedia = -1;
                m_indicatorY = parentIndicatorY(parentInsertIndexAt(pos));
            }
        } else if (e->mimeData()->hasFormat(kMediaMime)) {
            m_addChildMedia = -1;
            m_indicatorY = childIndicatorY(childInsertIndexAt(pos));   // hint a drop
        } else return;
        update();
        e->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent*) override { clearDnd(); }

    void dropEvent(QDropEvent* e) override
    {
        const QPoint pos = e->position().toPoint();
        if (e->mimeData()->hasFormat(kLayerMime)) {
            const int id = e->mimeData()->data(kLayerMime).toInt();
            if (e->modifiers() & Qt::AltModifier) {
                if (onChildDuplicate) onChildDuplicate(id, childInsertIndexAt(pos));
            } else if (onChildReorder) onChildReorder(id, childInsertIndexAt(pos));
        } else if (e->mimeData()->hasFormat(kParentMime)) {
            const int mid = e->mimeData()->data(kParentMime).toInt();
            const int zone = childZoneMediaAt(pos);
            if (zone >= 0 && zone != mid) { if (onAddChild) onAddChild(zone); }
            else if (onParentReorder)     onParentReorder(mid, parentInsertIndexAt(pos));
        } else if (e->mimeData()->hasFormat(kMediaMime)) {
            if (onMediaDropped) onMediaDropped(e->mimeData()->data(kMediaMime).toInt());
        }
        clearDnd();
        e->acceptProposedAction();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Tree connector lines: from under each expanded parent down to its
        // children, with a horizontal stub into each child.
        QPen line(QColor("#5D5D5D"));
        line.setWidthF(1.2);
        p.setPen(line);
        for (int i = 0; i < m_model.size(); ++i) {
            if (!m_model[i].isParent) continue;
            const QRect pr = m_model[i].w->geometry();
            const qreal x  = pr.left() + tree::trunkX();
            qreal lastY = -1;
            for (int j = i + 1; j < m_model.size() && !m_model[j].isParent; ++j) {
                const QRect cr = m_model[j].w->geometry();
                const qreal cy = cr.center().y();
                // Child content is indented (cr.left() is the full-width row edge);
                // the branch must reach toward the child's thumbnail, to the right.
                const qreal childX = cr.left() + tree::indent() - Ui::px(6);
                p.drawLine(QPointF(x, cy), QPointF(childX, cy));   // horizontal branch →
                lastY = cy;
            }
            if (lastY > 0)
                p.drawLine(QPointF(x, pr.bottom()), QPointF(x, lastY));   // vertical trunk ↓
        }

        // Add-child highlight (whole group)
        if (m_addChildMedia >= 0) {
            const QRect r = groupRect(m_addChildMedia);
            if (r.isValid()) {
                p.setPen(Qt::NoPen);
                QColor c("#FD5A1F"); c.setAlpha(40);
                p.setBrush(c);
                p.drawRoundedRect(r.adjusted(1, 1, -1, -1), Ui::px(8), Ui::px(8));
            }
        }
        // Reorder indicator line
        if (m_indicatorY >= 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#FD5A1F"));
            p.drawRoundedRect(QRectF(2, m_indicatorY - 1.5, width() - 4, 3), 1.5, 1.5);
        }
    }

private:
    void clearDnd() { m_indicatorY = -1; m_addChildMedia = -1; update(); }

    // Children insert index = number of child rows whose centre is above pos.y.
    int childInsertIndexAt(const QPoint& pos) const
    {
        int idx = 0;
        for (const Row& r : m_model) {
            if (r.isParent) continue;
            if (pos.y() < r.w->geometry().center().y()) return idx;
            ++idx;
        }
        return idx;
    }

    int parentInsertIndexAt(const QPoint& pos) const
    {
        int idx = 0;
        for (const Row& r : m_model) {
            if (!r.isParent) continue;
            if (pos.y() < r.w->geometry().center().y()) return idx;
            ++idx;
        }
        return idx;
    }

    // If pos is over a parent's body (its row or the children beneath it, before
    // the next parent), return that parent's mediaId; else -1.
    int childZoneMediaAt(const QPoint& pos) const
    {
        int curMedia = -1; int curTop = 0;
        for (int i = 0; i < m_model.size(); ++i) {
            const QRect g = m_model[i].w->geometry();
            if (m_model[i].isParent) {
                // Reorder near the top edge of a parent header (a thin band).
                if (pos.y() < g.top() + Ui::px(8)) return -1;
                curMedia = m_model[i].mediaId;
            }
            curTop = g.bottom();
            const bool last = (i + 1 >= m_model.size());
            const int nextTop = last ? height() : m_model[i + 1].w->geometry().top();
            if (pos.y() >= g.top() && pos.y() < nextTop) return curMedia;
        }
        return curMedia;
    }

    QRect groupRect(int mediaId) const
    {
        QRect r;
        bool active = false;
        for (int i = 0; i < m_model.size(); ++i) {
            if (m_model[i].isParent) active = (m_model[i].mediaId == mediaId);
            if (active && m_model[i].mediaId == mediaId)
                r = r.isNull() ? m_model[i].w->geometry()
                               : r.united(m_model[i].w->geometry());
        }
        return r;
    }

    int childIndicatorY(int childIdx) const
    {
        int seen = 0;
        const Row* last = nullptr;
        for (const Row& r : m_model) {
            if (r.isParent) continue;
            if (seen == childIdx) return qMax(2, r.w->geometry().top() - 3);
            last = &r; ++seen;
        }
        return last ? last->w->geometry().bottom() + 3 : 2;
    }

    int parentIndicatorY(int parentIdx) const
    {
        int seen = 0;
        const Row* last = nullptr;
        for (const Row& r : m_model) {
            if (!r.isParent) continue;
            if (seen == parentIdx) return qMax(2, r.w->geometry().top() - 3);
            last = &r; ++seen;
        }
        return last ? last->w->geometry().bottom() + 3 : 2;
    }

    QVBoxLayout*  m_layout       = nullptr;
    QVector<Row>  m_model;
    int           m_indicatorY   = -1;
    int           m_addChildMedia = -1;
};

// ============================================================
//  ParentRow — a source image (group header); not in the frame
// ============================================================

class ParentRow : public QFrame
{
public:
    std::function<void()>               onSelected;
    std::function<void(bool)>           onCollapse;       // collapsed?
    std::function<void(bool)>           onGroupVisible;   // visible?
    std::function<void()>               onAddChild;
    std::function<void()>               onDuplicate;
    std::function<void()>               onDelete;
    std::function<void(const QString&)> onRenamed;

    ParentRow(int mediaId, QWidget* parent = nullptr)
        : QFrame(parent), m_mediaId(mediaId)
    {
        // Outer row is transparent; the bordered "box" wraps chevron+thumb+name
        // and stops before the eye, which sits in a 70px gutter (like child rows).
        setObjectName("parentRowOuter");
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(Ui::px(52));

        auto* hl = new QHBoxLayout(this);
        hl->setContentsMargins(0, Ui::px(2), 0, Ui::px(2));
        hl->setSpacing(0);

        m_box = new QFrame(this);
        m_box->setObjectName("parentRow");
        // Margins/spacing line the thumb (x=38) and name (x=94) up with the
        // child rows' (indent 30 + pill margin 8 + thumb 46 + spacing 10).
        auto* bl = new QHBoxLayout(m_box);
        bl->setContentsMargins(Ui::px(10), Ui::px(4), Ui::px(12), Ui::px(4));
        bl->setSpacing(Ui::px(10));

        m_chevron = new ChevronButton(ChevronButton::Down);
        // Not "iconBtn": that QSS pins min/max-width to 24px fixed, which beats
        // setFixedSize and would push the thumb off the child rows' alignment.
        m_chevron->setObjectName("parentChevron");
        m_chevron->setFixedSize(Ui::px(18), Ui::px(34));
        m_chevron->setCursor(Qt::PointingHandCursor);
        connect(m_chevron, &QPushButton::clicked, this, [this]() {
            m_collapsed = !m_collapsed;
            m_chevron->setDirection(m_collapsed ? ChevronButton::Right : ChevronButton::Down);
            if (onCollapse) onCollapse(m_collapsed);
        });
        bl->addWidget(m_chevron);

        m_thumb = new QLabel;
        m_thumb->setFixedSize(Ui::px(46), Ui::px(32));   // same cell as child rows
        m_thumb->setAttribute(Qt::WA_TransparentForMouseEvents);
        bl->addWidget(m_thumb);

        m_name = new QLabel;
        m_name->setObjectName("layerName");
        m_name->setAttribute(Qt::WA_TransparentForMouseEvents);
        bl->addWidget(m_name, 1);

        m_nameEdit = new QLineEdit;
        m_nameEdit->setObjectName("layerNameEdit");
        m_nameEdit->setFrame(false);
        m_nameEdit->setVisible(false);
        m_nameEdit->setStyleSheet(QString(
            "background:transparent; border:none; padding:0; margin:0; min-height:0;"
            " color:#E3E3E3; font-size:%1px; font-weight:400;"
            " selection-background-color:#FD5A1F;").arg(Ui::px(18)));
        connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() { finishRename(); });
        bl->addWidget(m_nameEdit, 1);

        hl->addWidget(m_box, 1);

        m_eye = new QPushButton;
        m_eye->setObjectName("eyeBtn");
        m_eye->setFixedSize(Ui::px(30), Ui::px(30));
        m_eye->setCursor(Qt::PointingHandCursor);
        m_eye->setIconSize(QSize(Ui::px(20), Ui::px(20)));
        auto* eyeWrap = new QWidget(this);
        eyeWrap->setFixedWidth(Ui::px(70));
        auto* ew = new QHBoxLayout(eyeWrap);
        ew->setContentsMargins(0, 0, 0, 0);
        ew->addWidget(m_eye, 0, Qt::AlignCenter);
        hl->addWidget(eyeWrap);
        connect(m_eye, &QPushButton::clicked, this, [this]() {
            setGroupVisible(!m_visible);
            if (onGroupVisible) onGroupVisible(m_visible);
        });
    }

    int  mediaId() const { return m_mediaId; }
    void setThumb(const QPixmap& px) { m_thumb->setPixmap(px); }
    void setName(const QString& n)   { m_name->setText(n); }
    void setCollapsed(bool c)
    {
        m_collapsed = c;
        m_chevron->setDirection(c ? ChevronButton::Right : ChevronButton::Down);
    }
    void setGroupVisible(bool on)
    {
        m_visible = on;
        m_eye->setIcon(QIcon(on ? ":/icons/eye_open.svg" : ":/icons/eye_closed.svg"));
    }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && !m_editing) {
            m_pressPos = e->pos();
            if (onSelected) onSelected();
        }
        QFrame::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_editing) return;
        if (!(e->buttons() & Qt::LeftButton)) return;
        if ((e->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance())
            return;
        auto* mime = new QMimeData;
        mime->setData(kParentMime, QByteArray::number(m_mediaId));
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(grab());
        drag->setHotSpot(m_pressPos);
        drag->exec(Qt::MoveAction);
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_box->geometry().contains(e->pos())) {
            beginRename();
            e->accept();
            return;
        }
        QFrame::mouseDoubleClickEvent(e);
    }
    void contextMenuEvent(QContextMenuEvent* e) override
    {
        QMenu menu(this);
        QAction* add = menu.addAction("Add child (in frame)");
        QAction* ren = menu.addAction("Rename");
        QAction* dup = menu.addAction("Duplicate image");
        menu.addSeparator();
        QAction* del = menu.addAction("Delete image");
        QAction* chosen = menu.exec(e->globalPos());
        if      (chosen == add && onAddChild)  onAddChild();
        else if (chosen == ren)                beginRename();
        else if (chosen == dup && onDuplicate) onDuplicate();
        else if (chosen == del && onDelete)    onDelete();
    }

private:
    void beginRename()
    {
        if (m_editing) return;
        m_editing = true;
        m_nameEdit->setText(m_name->text());
        m_name->hide();
        m_nameEdit->show();
        m_nameEdit->setFocus(Qt::MouseFocusReason);
        m_nameEdit->selectAll();
    }
    void finishRename()
    {
        if (!m_editing) return;
        m_editing = false;
        const QString text = m_nameEdit->text().trimmed();
        if (!text.isEmpty() && text != m_name->text()) {
            setName(text);
            if (onRenamed) onRenamed(text);
        }
        m_nameEdit->hide();
        m_name->show();
    }

    int            m_mediaId;
    bool           m_collapsed = false;
    bool           m_visible   = true;
    bool           m_editing   = false;
    QFrame*        m_box       = nullptr;
    ChevronButton* m_chevron   = nullptr;
    QLabel*        m_thumb     = nullptr;
    QLabel*        m_name      = nullptr;
    QLineEdit*     m_nameEdit  = nullptr;
    QPushButton*   m_eye       = nullptr;
    QPoint         m_pressPos;
};

// ============================================================
//  TrashButton — click or drop a layer row to delete
// ============================================================

class TrashButton : public QPushButton
{
public:
    std::function<void(int)> onLayerDropped;   // layerId

    explicit TrashButton(QWidget* parent = nullptr)
        : QPushButton(parent)
    {
        setObjectName("trashBtn");
        setFixedSize(30, 30);
        setCursor(Qt::PointingHandCursor);
        setIcon(QIcon(":/icons/trash.svg"));
        setIconSize(QSize(16, 16));
        setAcceptDrops(true);
        setToolTip("Delete layer (click or drop a layer here)");
    }

protected:
    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasFormat(kLayerMime)) {
            setProperty("dropHover", true);
            repolish(this);
            e->acceptProposedAction();
        }
    }

    void dragLeaveEvent(QDragLeaveEvent*) override
    {
        setProperty("dropHover", false);
        repolish(this);
    }

    void dropEvent(QDropEvent* e) override
    {
        setProperty("dropHover", false);
        repolish(this);
        const int id = e->mimeData()->data(kLayerMime).toInt();
        if (onLayerDropped) onLayerDropped(id);
        e->acceptProposedAction();
    }
};

// ============================================================
//  LayersPanel
// ============================================================

LayersPanel::LayersPanel(bool embedded, QWidget* parent)
    : QWidget(parent), m_embedded(embedded)
{
    if (m_embedded) {
        // Embedded: just the scrollable list of rows. The "Layers" header,
        // separators and the surrounding column chrome belong to ControlsPanel.
        // Fusion lives in the right panel; deletion is via Backspace.
        setAttribute(Qt::WA_StyledBackground, true);
        setObjectName("layersEmbedded");

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* scroll = new QScrollArea;
        scroll->setObjectName("layersScroll");
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameShape(QFrame::NoFrame);
        m_rowsScroll = scroll;

        m_rowsArea = new RowsArea;
        m_rowsArea->onChildReorder    = [this](int id,  int idx) { emit reorderRequested(id, idx); };
        m_rowsArea->onChildDuplicate  = [this](int id,  int idx) { emit duplicateChildRequested(id, idx); };
        m_rowsArea->onParentReorder = [this](int mid, int idx) { emit parentReordered(mid, idx); };
        m_rowsArea->onAddChild      = [this](int mid)          { emit addChildRequested(mid); };
        m_rowsArea->onMediaDropped  = [this](int mid)          { emit mediaDroppedAsLayer(mid); };
        // Left margin aligns the thumbnail with the section titles (40px); the
        // row's own 70px eye gutter sits flush against the right content edge.
        // Top = 0: the "Layers" header already adds 12px below its title and each
        // row adds 2px above its pill → 14px gap, matching the right column's
        // section-title → first-control spacing (12 title pad + 2 body top).
        m_rowsArea->rowsLayout()->setContentsMargins(Ui::px(32), 0, 0, Ui::px(2));
        m_rowsArea->rowsLayout()->setSpacing(Ui::px(6));
        scroll->setWidget(m_rowsArea);
        // Floating scrollbar: reserves no width, so rows keep a constant width
        // whether or not the list overflows (no shrink, no rebuild overlap).
        installOverlayScrollbar(scroll);

        root->addWidget(scroll, 1);
        m_isExpanded = true;
        return;
    }

    setAttribute(Qt::WA_StyledBackground, false);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    // Keep the floating panel sized to its content at all times.
    root->setSizeConstraint(QLayout::SetFixedSize);

    // ── Expanded panel ───────────────────────────────────────
    m_expandedBox = new QWidget;
    m_expandedBox->setObjectName("layersPanel");
    m_expandedBox->setAttribute(Qt::WA_StyledBackground, true);
    m_expandedBox->setFixedWidth(kPanelWidth);
    {
        auto* vl = new QVBoxLayout(m_expandedBox);
        vl->setContentsMargins(12, 10, 12, 12);
        vl->setSpacing(10);

        m_headerWidget = new QWidget;
        m_headerWidget->setObjectName("layersHeader");
        m_headerWidget->setCursor(Qt::OpenHandCursor);
        auto* header = new QHBoxLayout(m_headerWidget);
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(6);
        header->addWidget(makeSectionTitle("Layers"), 1, Qt::AlignVCenter);

        auto* addBtn = makeIconButton(":/icons/plus.svg");
        addBtn->setToolTip("Add layer (duplicates the selected one)");
        connect(addBtn, &QPushButton::clicked, this, [this]() { emit addLayerRequested(); });
        header->addWidget(addBtn);

        auto* collapseBtn = new ChevronButton(ChevronButton::Right);
        connect(collapseBtn, &QPushButton::clicked, this, [this]() { setExpandedUi(false); });
        header->addWidget(collapseBtn);
        vl->addWidget(m_headerWidget);

        m_rowsArea = new RowsArea;
        m_rowsArea->onChildReorder    = [this](int id,  int idx) { emit reorderRequested(id, idx); };
        m_rowsArea->onChildDuplicate  = [this](int id,  int idx) { emit duplicateChildRequested(id, idx); };
        m_rowsArea->onParentReorder = [this](int mid, int idx) { emit parentReordered(mid, idx); };
        m_rowsArea->onAddChild      = [this](int mid)          { emit addChildRequested(mid); };
        m_rowsArea->onMediaDropped  = [this](int mid)          { emit mediaDroppedAsLayer(mid); };
        vl->addWidget(m_rowsArea);

        auto* footer = new QHBoxLayout;
        footer->setContentsMargins(0, 0, 0, 0);
        footer->setSpacing(6);

        m_blendCombo = new NoWheelComboBox;
        m_blendCombo->setObjectName("blendCombo");
        for (const auto& e : kBlendEntries) {
            if (e.groupStart && m_blendCombo->count() > 0)
            m_blendCombo->insertSeparator(m_blendCombo->count());
            m_blendCombo->addItem(QString::fromUtf8(e.name), int(e.mode));
        }
        connect(m_blendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
            if (m_updating) return;
            const QVariant v = m_blendCombo->currentData();
            if (v.isValid())
                emit blendModeChanged(m_activeId, static_cast<BlendMode>(v.toInt()));
        });
        footer->addWidget(m_blendCombo, 1);

        m_trashBtn = new TrashButton;
        m_trashBtn->onLayerDropped = [this](int id) { emit deleteRequested(id); };
        connect(m_trashBtn, &QPushButton::clicked, this, [this]() {
            emit deleteRequested(m_activeId);
        });
        footer->addWidget(m_trashBtn);

        vl->addLayout(footer);
    }
    root->addWidget(m_expandedBox);

    // ── Collapsed button ─────────────────────────────────────
    m_collapsedBtn = new QPushButton;
    m_collapsedBtn->setObjectName("layersToggleBtn");
    m_collapsedBtn->setFixedSize(38, 38);
    m_collapsedBtn->setCursor(Qt::PointingHandCursor);
    m_collapsedBtn->setIcon(QIcon(":/icons/layers.svg"));
    m_collapsedBtn->setIconSize(QSize(19, 19));
    m_collapsedBtn->setToolTip("Layers");
    connect(m_collapsedBtn, &QPushButton::clicked, this, [this]() { setExpandedUi(true); });
    root->addWidget(m_collapsedBtn);

    // Start collapsed — only the small toggle button shows until the user
    // opens the panel.
    m_isExpanded = false;
    m_expandedBox->setVisible(false);
    m_collapsedBtn->setVisible(true);

    if (parent) parent->installEventFilter(this);
    if (m_headerWidget) m_headerWidget->installEventFilter(this);
}

void LayersPanel::requestAddLayer() { emit addLayerRequested(); }

void LayersPanel::setExpandedUi(bool expanded)
{
    m_isExpanded = expanded;
    m_expandedBox->setVisible(expanded);
    m_collapsedBtn->setVisible(!expanded);
    if (expanded) {
        if (m_hasCustomPosition) move(m_customPosition);
        else reposition();
    } else {
        // when collapsed, reset custom position so it returns to anchored spot
        m_hasCustomPosition = false;
        reposition();
    }
}

void LayersPanel::setTree(const std::vector<ParentGroup>& parents,
                          const std::vector<Layer>& layers, int activeId,
                          const QHash<int, QImage>& mediaImages)
{
    m_parents     = parents;
    m_layers      = layers;
    m_activeId    = activeId;
    m_mediaImages = mediaImages;
    refreshRows();
    syncFooter();
    reposition();
}

void LayersPanel::setSelection(const QSet<int>& sel)
{
    m_selSet = sel;
    for (LayerRow* r : m_rows)
        r->setSelected(r->layerId() == m_activeId || m_selSet.contains(r->layerId()));
}

void LayersPanel::setLayers(const std::vector<Layer>& layers, int activeId)
{
    m_parents.clear();
    m_mediaImages.clear();
    m_layers   = layers;
    m_activeId = activeId;
    refreshRows();
    syncFooter();
    reposition();
}

// A signature of the tree's structure (groups, order, collapse, child ids). When
// it's unchanged we update thumbs/state in place; otherwise we rebuild widgets.
// Rebuilding only on structural change avoids killing a row mid-gesture and
// stops paint churn on the periodic live-thumb refresh.
static QString treeSignature(const std::vector<ParentGroup>& parents,
                             const std::vector<Layer>& layers)
{
    QString s;
    for (const auto& p : parents)
        s += QString("P%1%2;").arg(p.mediaId).arg(p.collapsed ? 'c' : 'o');
    for (const auto& l : layers)
        s += QString("L%1@%2;").arg(l.id).arg(l.mediaId);
    return s;
}

void LayersPanel::refreshRows()
{
    const QString sig = treeSignature(m_parents, m_layers);
    if (sig == m_treeSig && !m_rows.isEmpty())
        updateRowsInPlace();
    else { m_treeSig = sig; buildTree(); }
}

void LayersPanel::setSourceImage(const QImage& source)
{
    // Legacy single-source fallback (used when no per-media images are supplied).
    m_smallSource = source.isNull()
        ? QImage()
        : source.scaled(92, 64, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    m_treeSig.clear();   // force rebuild so thumbs refresh
    refreshRows();
    syncFooter();
    reposition();
}

void LayersPanel::setBackground(const QColor& background, float opacity)
{
    // Store only — the following setTree() re-renders thumbs (in place) with it.
    m_background = background;
    m_bgOpacity = opacity;
}

QPixmap LayersPanel::thumbFor(const Layer& layer) const
{
    QImage src = m_mediaImages.value(layer.mediaId);
    if (src.isNull()) src = m_smallSource;
    const QImage ref = src.isNull() ? QImage() : RenderWorker::renderLayer(src, layer);
    return roundedThumb(ref, QSize(Ui::px(44), Ui::px(30)), Ui::px(5), m_background, m_bgOpacity);
}

QPixmap LayersPanel::parentThumb(int mediaId) const
{
    return roundedThumb(m_mediaImages.value(mediaId), QSize(Ui::px(44), Ui::px(30)),
                        Ui::px(5), m_background, m_bgOpacity);
}

void LayersPanel::buildTree()
{
    QVBoxLayout* lay = m_rowsArea->rowsLayout();
    while (QLayoutItem* it = lay->takeAt(0)) {
        if (QWidget* w = it->widget()) { w->hide(); w->deleteLater(); }
        delete it;
    }
    m_rows.clear();
    m_parentRows.clear();
    QVector<RowsArea::Row> model;

    auto addChildRow = [&](const Layer& layer) {
        auto* row = new LayerRow(layer.id);
        row->setName(layer.name);
        row->setThumb(thumbFor(layer));
        row->setLayerVisible(layer.visible);
        row->setLocked(layer.locked);
        row->setSelected(layer.id == m_activeId || m_selSet.contains(layer.id));
        row->setIndent(tree::indent());
        row->setHasEdits(layer.kind != LayerKind::Original);
        const int id = layer.id;
        row->onSelected        = [this, id]()                { emit layerSelected(id); };
        row->onShiftSelected   = [this, id]()                { emit layerRangeRequested(id); };
        row->onCtrlSelected    = [this, id]()                { emit layerToggleRequested(id); };
        row->onEyeToggled      = [this, id](bool on)         { emit visibilityToggled(id, on); };
        row->onLockToggled     = [this, id](bool on)         { emit lockToggled(id, on); };
        row->onRenamed         = [this, id](const QString& n){ emit layerRenamed(id, n); };
        row->onDeleteRequested = [this, id]()                { emit deleteRequested(id); };
        row->onRemoveEditsRequested = [this, id]()           { emit removeEditsRequested(id); };
        row->onCopyRequested   = [this, id]()                { emit copyLayerRequested(id); };
        row->onPasteRequested  = [this, id]()                { emit pasteLayerRequested(id); };
        lay->addWidget(row);
        m_rows.append(row);
        model.push_back({ row, false, layer.mediaId, id });
    };

    if (m_parents.empty()) {
        // Legacy flat list (no groups supplied).
        for (const Layer& l : m_layers) addChildRow(l);
    } else {
        for (const ParentGroup& g : m_parents) {
            auto* prow = new ParentRow(g.mediaId);
            prow->setName(g.name);
            prow->setThumb(parentThumb(g.mediaId));
            prow->setCollapsed(g.collapsed);
            prow->setGroupVisible(g.groupVisible);
            const int mid = g.mediaId;
            prow->onCollapse     = [this, mid](bool c)  { emit collapseToggled(mid, c); };
            prow->onGroupVisible = [this, mid](bool v)  { emit groupVisibilityToggled(mid, v); };
            prow->onAddChild     = [this, mid]()        { emit addChildRequested(mid); };
            prow->onDuplicate    = [this, mid]()        { emit duplicateParentRequested(mid); };
            prow->onDelete       = [this, mid]()        { emit deleteParentRequested(mid); };
            prow->onRenamed      = [this, mid](const QString& n) { emit parentRenamed(mid, n); };
            lay->addWidget(prow);
            m_parentRows.append(prow);
            model.push_back({ prow, true, mid, -1 });

            if (!g.collapsed)
                for (const Layer& l : m_layers)
                    if (l.mediaId == g.mediaId) addChildRow(l);
        }
    }

    if (m_embedded) lay->addStretch(1);
    m_rowsArea->setModel(model);
}

void LayersPanel::updateRowsInPlace()
{
    int ci = 0;
    auto updateChild = [&](const Layer& layer) {
        if (ci >= m_rows.size()) return;
        LayerRow* row = m_rows[ci++];
        row->setName(layer.name);
        row->setThumb(thumbFor(layer));
        row->setLayerVisible(layer.visible);
        row->setLocked(layer.locked);
        row->setSelected(layer.id == m_activeId || m_selSet.contains(layer.id));
        // Mode switches don't change treeSignature() (same layer ids/structure),
        // so this in-place path runs on every mode change too — must refresh
        // "has edits" here, or the context menu keeps whatever kind was current
        // the last time buildTree() ran (e.g. still offers "Remove edits" after
        // reverting to Original).
        row->setHasEdits(layer.kind != LayerKind::Original);
    };
    if (m_parents.empty()) {
        for (const Layer& l : m_layers) updateChild(l);
    } else {
        for (const ParentGroup& g : m_parents)
            if (!g.collapsed)
                for (const Layer& l : m_layers)
                    if (l.mediaId == g.mediaId) updateChild(l);
    }
    for (int i = 0; i < m_parentRows.size() && i < int(m_parents.size()); ++i) {
        m_parentRows[i]->setName(m_parents[i].name);
        m_parentRows[i]->setThumb(parentThumb(m_parents[i].mediaId));
        m_parentRows[i]->setGroupVisible(m_parents[i].groupVisible);
    }
}

void LayersPanel::syncFooter()
{
    if (m_embedded || !m_blendCombo) return;   // no blend/trash footer when embedded

    const int idx = findLayerById(m_layers, m_activeId);

    m_updating = true;
    if (idx >= 0) {
        const int target = int(m_layers[idx].blend);
        for (int i = 0; i < m_blendCombo->count(); ++i) {
            const QVariant v = m_blendCombo->itemData(i);
            if (v.isValid() && v.toInt() == target) {
                m_blendCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    m_updating = false;

    m_blendCombo->setEnabled(idx >= 0);
    m_trashBtn->setEnabled(idx >= 0 && m_layers[idx].kind != LayerKind::Original);
}

void LayersPanel::reposition()
{
    if (m_embedded) return;   // laid out by the column, never self-positioned
    QWidget* p = parentWidget();
    if (!p) return;
    move(p->width() - width() - kMargin, kMargin);
    raise();
}

bool LayersPanel::eventFilter(QObject* obj, QEvent* ev)
{
    if (m_embedded) return QWidget::eventFilter(obj, ev);

    if (obj == m_headerWidget) {
        if (ev->type() == QEvent::MouseButtonPress) {
            auto* mev = static_cast<QMouseEvent*>(ev);
            if (mev->button() == Qt::LeftButton) {
                m_dragStart = mev->globalPosition().toPoint() - pos();
                m_headerWidget->setCursor(Qt::ClosedHandCursor);
                return true;
            }
        } else if (ev->type() == QEvent::MouseMove) {
            auto* mev = static_cast<QMouseEvent*>(ev);
            if (mev->buttons() & Qt::LeftButton) {
                QPoint newPos = mev->globalPosition().toPoint() - m_dragStart;
                move(newPos);
                m_hasCustomPosition = true;
                m_customPosition = newPos;
                return true;
            }
        } else if (ev->type() == QEvent::MouseButtonRelease) {
            m_headerWidget->setCursor(Qt::OpenHandCursor);
            return true;
        }
    }

    if (obj == parentWidget()
        && (ev->type() == QEvent::Resize || ev->type() == QEvent::Show)) {
        if (!m_hasCustomPosition) reposition();
        else raise();
    }
    return QWidget::eventFilter(obj, ev);
}

void LayersPanel::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    if (!m_embedded) reposition();
}
