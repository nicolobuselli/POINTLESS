#include "ControlsPanel.h"
#include "AdjustmentsPanel.h"
#include "LayersPanel.h"
#include "Widgets.h"
#include "UiScale.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QIcon>

namespace {

// Full-width 1px divider used to bracket the section titles.
QFrame* bandLine()
{
    auto* f = new QFrame;
    f->setObjectName("bandLine");
    f->setFixedHeight(1);
    return f;
}

// A fixed section-title band: title text in a row with vertical padding.
QWidget* titleBand(const QString& text)
{
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(Ui::px(40), Ui::px(12), Ui::px(40), Ui::px(12));
    l->setSpacing(0);
    l->addWidget(makeSectionTitle(text), 1);
    return w;
}

} // namespace

ControlsPanel::ControlsPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("sidePanel");
    setMinimumWidth(Ui::px(420));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── File name (editable, click to rename, Enter confirms) ────
    {
        auto* nameRow = new QWidget;
        auto* nl = new QHBoxLayout(nameRow);
        nl->setContentsMargins(Ui::px(40), Ui::px(14), Ui::px(20), Ui::px(14));   // aligned with "Layers"
        nl->setSpacing(0);

        m_fileTitle = new QLineEdit;
        m_fileTitle->setObjectName("fileTitleEdit");
        m_fileTitle->setFrame(false);
        m_fileTitle->setPlaceholderText("add title…");
        m_fileTitle->setFocusPolicy(Qt::ClickFocus);   // no caret until clicked
        connect(m_fileTitle, &QLineEdit::returnPressed, this, [this]() {
            m_fileTitle->clearFocus();
        });
        connect(m_fileTitle, &QLineEdit::editingFinished, this, [this]() {
            if (!m_updating) emit fileRenamed(m_fileTitle->text().trimmed());
        });
        nl->addWidget(m_fileTitle, 1);
        outer->addWidget(nameRow);
    }

    outer->addWidget(bandLine());

    // Layers / Parameters are split by a draggable handle so the user can
    // decide how much height each gets.
    auto* split = new QSplitter(Qt::Vertical);
    split->setObjectName("leftSplit");
    split->setChildrenCollapsible(false);
    split->setHandleWidth(Ui::px(3));

    // ── Layers pane (header + scrollable embedded list) ──────────
    auto* layersPane = new QWidget;
    {
        auto* ll = new QVBoxLayout(layersPane);
        ll->setContentsMargins(0, 0, 0, 0);
        ll->setSpacing(0);

        // "Layers" header with a "+" in the gutter to add a layer (a new layer
        // of the current mode, or a duplicate of the active one).
        {
            auto* hdr = new QWidget;
            auto* hl = new QHBoxLayout(hdr);
            hl->setContentsMargins(Ui::px(40), Ui::px(12), 0, Ui::px(12));
            hl->setSpacing(0);
            hl->addWidget(makeSectionTitle("Layers"), 1);
            // "+" centred in a 70px gutter so it lines up with the layer eyes.
            auto* gut = new QWidget;
            gut->setFixedWidth(Ui::px(70));
            auto* gl = new QHBoxLayout(gut);
            gl->setContentsMargins(0, 0, 0, 0);
            auto* add = new QPushButton;
            add->setObjectName("iconBtn");
            add->setCursor(Qt::PointingHandCursor);
            add->setFixedSize(Ui::px(26), Ui::px(26));
            add->setIcon(QIcon(":/icons/plus.svg"));
            add->setIconSize(QSize(Ui::px(16), Ui::px(16)));
            add->setToolTip("Add layer");
            connect(add, &QPushButton::clicked, this, [this]() { m_layers->requestAddLayer(); });
            gl->addWidget(add, 0, Qt::AlignCenter);
            hl->addWidget(gut);
            ll->addWidget(hdr);
        }
        ll->addWidget(bandLine());
        m_layers = new LayersPanel(/*embedded*/ true);
        m_layers->setObjectName("layersEmbedded");
        m_layers->setMinimumHeight(Ui::px(64));   // at least ~1 row, then scroll
        ll->addWidget(m_layers, 1);

        // ── Frame dimensions (W × H of the canvas) ───────────────
        {
            auto* fw = new QWidget;
            auto* fl = new QVBoxLayout(fw);
            fl->setContentsMargins(Ui::px(40), Ui::px(10), Ui::px(40), Ui::px(16));
            fl->setSpacing(Ui::px(8));
            fl->addWidget(makeParamLabel("Frame dimensions"));

            auto* row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(Ui::px(12));
            m_frameW = new DragSpinBox("", 16, 8192, 1080);
            m_frameH = new DragSpinBox("", 16, 8192, 1080);
            m_frameW->setTextLabel("W");
            m_frameH->setTextLabel("H");
            auto emitFrame = [this](int) {
                if (!m_updating) emit frameSizeChanged(m_frameW->value(), m_frameH->value());
            };
            m_frameW->onValueChanged = emitFrame;
            m_frameH->onValueChanged = emitFrame;
            row->addWidget(m_frameW, 1);
            row->addWidget(m_frameH, 1);
            fl->addLayout(row);
            ll->addWidget(fw);
        }
    }

    // ── Parameters pane (header + scrollable adjustments) ────────
    auto* paramsPane = new QWidget;
    {
        auto* pp = new QVBoxLayout(paramsPane);
        pp->setContentsMargins(0, 0, 0, 0);
        pp->setSpacing(0);
        pp->addWidget(titleBand("Parameters"));
        pp->addWidget(bandLine());
        m_adjust = new AdjustmentsPanel;
        connect(m_adjust, &AdjustmentsPanel::adjustmentsChanged,
                this, &ControlsPanel::adjustmentsChanged);
        connect(m_adjust, &AdjustmentsPanel::resetRequested,
                this, &ControlsPanel::resetRequested);
        pp->addWidget(m_adjust, 1);
        paramsPane->setMinimumHeight(Ui::px(120));
    }

    split->addWidget(layersPane);
    split->addWidget(paramsPane);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    // Default: Layers shows ~4 rows (title + bandLine + 4 rows), Parameters the rest.
    split->setSizes({ Ui::px(4 * 52 + 3 * 6 + 58), Ui::px(900) });
    outer->addWidget(split, 1);
}

void ControlsPanel::setFileName(const QString& name)
{
    m_updating = true;
    m_fileTitle->setText(name);          // empty → "add title…" placeholder
    m_fileTitle->setCursorPosition(0);
    m_updating = false;
}

void ControlsPanel::setFrameSize(int w, int h)
{
    m_updating = true;
    if (m_frameW) m_frameW->setValue(w);
    if (m_frameH) m_frameH->setValue(h);
    m_updating = false;
}

// ── Parameters ───────────────────────────────────────────────

Adjustments ControlsPanel::adjustments() const          { return m_adjust->adjustments(); }
void ControlsPanel::setAdjustments(const Adjustments& a) { m_adjust->setAdjustments(a); }
void ControlsPanel::setSourceImage(const QImage& img)    { m_adjust->setSourceImage(img); }
