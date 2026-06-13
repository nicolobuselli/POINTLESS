#include "LayersPanel.h"
#include "Widgets.h"
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
#include <QStyle>
#include <QLineEdit>

namespace {

constexpr int  kPanelWidth = 240;
constexpr int  kMargin     = 12;
const char*    kLayerMime  = "application/x-ultraditherer-layer";

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
    std::function<void(bool)>       onEyeToggled;
    std::function<void(const QString&)> onRenamed;

    LayerRow(int layerId, QWidget* parent = nullptr)
        : QFrame(parent), m_id(layerId)
    {
        setObjectName("layerRow");
        setProperty("selected", false);
        setFixedHeight(44);
        setCursor(Qt::PointingHandCursor);

        auto* hl = new QHBoxLayout(this);
        hl->setContentsMargins(6, 0, 8, 0);
        hl->setSpacing(8);

        m_thumb = new QLabel;
        m_thumb->setFixedSize(46, 32);
        m_thumb->setStyleSheet("background: transparent;");
        m_thumb->setAttribute(Qt::WA_TransparentForMouseEvents);
        hl->addWidget(m_thumb);

        m_name = new QLabel;
        m_name->setObjectName("layerName");
        m_name->setTextInteractionFlags(Qt::NoTextInteraction);
        m_name->setAttribute(Qt::WA_TransparentForMouseEvents);
        hl->addWidget(m_name, 1);

        m_nameEdit = new QLineEdit(this);
        m_nameEdit->setObjectName("layerNameEdit");
        m_nameEdit->setFrame(false);
        m_nameEdit->setVisible(false);
        m_nameEdit->setAlignment(m_name->alignment());
        connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
            finishNameEditing();
        });

        m_eye = new QPushButton;
        m_eye->setObjectName("eyeBtn");
        m_eye->setFixedSize(26, 26);
        m_eye->setCursor(Qt::PointingHandCursor);
        m_eye->setIconSize(QSize(17, 17));
        hl->addWidget(m_eye);

        connect(m_eye, &QPushButton::clicked, this, [this]() {
            setLayerVisible(!m_visible);
            if (onEyeToggled) onEyeToggled(m_visible);
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

    void setSelected(bool sel)
    {
        if (property("selected").toBool() == sel) return;
        setProperty("selected", sel);
        repolish(this);
        repolish(m_name);
    }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && !m_editingName) {
            m_pressPos = e->pos();
            if (onSelected) onSelected();
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
        if (e->button() == Qt::LeftButton && m_name->geometry().contains(e->pos())) {
            beginNameEditing();
            e->accept();
            return;
        }
        QFrame::mouseDoubleClickEvent(e);
    }

private:
    void beginNameEditing()
    {
        if (m_editingName) return;
        m_editingName = true;
        m_nameEdit->setText(m_name->text());
        m_nameEdit->setGeometry(m_name->geometry());
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
    bool         m_editingName = false;
    QLabel*      m_thumb   = nullptr;
    QLabel*      m_name    = nullptr;
    QLineEdit*   m_nameEdit = nullptr;
    QPushButton* m_eye     = nullptr;
    QPoint       m_pressPos;
};

// ============================================================
//  RowsArea — hosts the rows; accepts drops for reordering
// ============================================================

class RowsArea : public QWidget
{
public:
    std::function<void(int, int)> onReorder;   // layerId, insertIndex

    explicit RowsArea(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAcceptDrops(true);
        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(6);
    }

    QVBoxLayout* rowsLayout() const { return m_layout; }

protected:
    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasFormat(kLayerMime))
            e->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent* e) override
    {
        if (!e->mimeData()->hasFormat(kLayerMime)) return;
        const int idx = insertIndexAt(e->position().toPoint());
        const int y   = indicatorYFor(idx);
        if (y != m_indicatorY) {
            m_indicatorY = y;
            update();
        }
        e->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent*) override
    {
        m_indicatorY = -1;
        update();
    }

    void dropEvent(QDropEvent* e) override
    {
        m_indicatorY = -1;
        update();
        const int id  = e->mimeData()->data(kLayerMime).toInt();
        const int idx = insertIndexAt(e->position().toPoint());
        if (onReorder) onReorder(id, idx);
        e->acceptProposedAction();
    }

    void paintEvent(QPaintEvent*) override
    {
        if (m_indicatorY < 0) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#FD5A1F"));
        p.drawRoundedRect(QRectF(2, m_indicatorY - 1.5, width() - 4, 3), 1.5, 1.5);
    }

private:
    int insertIndexAt(const QPoint& pos) const
    {
        for (int i = 0; i < m_layout->count(); ++i) {
            QWidget* w = m_layout->itemAt(i)->widget();
            if (!w) continue;
            if (pos.y() < w->geometry().center().y()) return i;
        }
        return m_layout->count();
    }

    int indicatorYFor(int index) const
    {
        if (m_layout->count() == 0) return 0;
        if (index >= m_layout->count()) {
            QWidget* last = m_layout->itemAt(m_layout->count() - 1)->widget();
            return last ? last->geometry().bottom() + 3 : height() - 2;
        }
        QWidget* w = m_layout->itemAt(index)->widget();
        return w ? qMax(2, w->geometry().top() - 3) : 0;
    }

    QVBoxLayout* m_layout      = nullptr;
    int          m_indicatorY  = -1;
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

LayersPanel::LayersPanel(QWidget* parent)
    : QWidget(parent)
{
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

        auto* header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(6);
        header->addWidget(makeSectionTitle("Layers"), 1);

        auto* addBtn = makeIconButton(":/icons/plus.svg");
        addBtn->setToolTip("Add layer (duplicates the selected one)");
        connect(addBtn, &QPushButton::clicked, this, [this]() { emit addLayerRequested(); });
        header->addWidget(addBtn);

        auto* collapseBtn = new ChevronButton(ChevronButton::Right);
        connect(collapseBtn, &QPushButton::clicked, this, [this]() { setExpandedUi(false); });
        header->addWidget(collapseBtn);
        vl->addLayout(header);

        m_rowsArea = new RowsArea;
        m_rowsArea->onReorder = [this](int id, int idx) { emit reorderRequested(id, idx); };
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

    m_collapsedBtn->setVisible(false);

    if (parent) parent->installEventFilter(this);
}

void LayersPanel::setExpandedUi(bool expanded)
{
    m_isExpanded = expanded;
    m_expandedBox->setVisible(expanded);
    m_collapsedBtn->setVisible(!expanded);
    reposition();
}

void LayersPanel::setLayers(const std::vector<Layer>& layers, int activeId)
{
    m_layers   = layers;
    m_activeId = activeId;

    bool sameStructure = (int(m_rows.size()) == int(layers.size()));
    if (sameStructure) {
        for (int i = 0; i < int(layers.size()); ++i) {
            if (m_rows[i]->layerId() != layers[i].id) { sameStructure = false; break; }
        }
    }

    // Updating in place keeps the row widgets alive: rebuilding them on a
    // selection click would kill the widget mid-gesture and break dragging.
    if (sameStructure)
        updateRowsInPlace();
    else
        rebuildRows();

    syncFooter();
    reposition();
}

void LayersPanel::setSourceImage(const QImage& source)
{
    // Small working copy: row thumbs apply per-layer adjustments to it.
    m_smallSource = source.isNull()
        ? QImage()
        : source.scaled(92, 64, Qt::KeepAspectRatioByExpanding,
                        Qt::SmoothTransformation);
    rebuildRows();
    syncFooter();
    reposition();
}

void LayersPanel::setBackground(const QColor& background, float opacity)
{
    m_background = background;
    m_bgOpacity = opacity;
    rebuildRows();
    syncFooter();
    reposition();
}

QPixmap LayersPanel::thumbFor(const Layer& layer) const
{
    const QImage ref = m_smallSource.isNull()
        ? QImage()
        : RenderWorker::renderLayer(m_smallSource, layer);
    return roundedThumb(ref, QSize(46, 32), 4.0, m_background, m_bgOpacity);
}

void LayersPanel::rebuildRows()
{
    for (LayerRow* row : m_rows) {
        m_rowsArea->rowsLayout()->removeWidget(row);
        row->hide();
        row->deleteLater();
    }
    m_rows.clear();

    for (const Layer& layer : m_layers) {
        auto* row = new LayerRow(layer.id);
        row->setName(layer.name);
        row->setThumb(thumbFor(layer));
        row->setLayerVisible(layer.visible);
        row->setSelected(layer.id == m_activeId);

        const int id = layer.id;
        row->onSelected   = [this, id]()               { emit layerSelected(id); };
        row->onEyeToggled = [this, id](bool on)        { emit visibilityToggled(id, on); };
        row->onRenamed    = [this, id](const QString& name) { emit layerRenamed(id, name); };

        m_rowsArea->rowsLayout()->addWidget(row);
        m_rows.append(row);
    }
}

void LayersPanel::updateRowsInPlace()
{
    for (int i = 0; i < int(m_layers.size()); ++i) {
        const Layer& layer = m_layers[i];
        LayerRow* row = m_rows[i];
        row->setName(layer.name);
        row->setThumb(thumbFor(layer));
        row->setLayerVisible(layer.visible);
        row->setSelected(layer.id == m_activeId);
    }
}

void LayersPanel::syncFooter()
{
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
    QWidget* p = parentWidget();
    if (!p) return;
    move(p->width() - width() - kMargin, kMargin);
    raise();
}

bool LayersPanel::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == parentWidget()
        && (ev->type() == QEvent::Resize || ev->type() == QEvent::Show)) {
        reposition();
    }
    return QWidget::eventFilter(obj, ev);
}

void LayersPanel::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    reposition();
}
