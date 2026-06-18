#include "ControlsPanel.h"
#include "AdjustmentsPanel.h"
#include "LayersPanel.h"
#include "Widgets.h"
#include "UiScale.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

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
        nl->setContentsMargins(Ui::px(20), Ui::px(14), Ui::px(20), Ui::px(14));
        nl->setSpacing(0);

        m_fileTitle = new QLineEdit("Senza Titolo");
        m_fileTitle->setObjectName("fileTitleEdit");
        m_fileTitle->setFrame(false);
        m_fileTitle->setFocusPolicy(Qt::ClickFocus);   // no caret until clicked
        m_fileTitle->setCursorPosition(0);
        connect(m_fileTitle, &QLineEdit::returnPressed, this, [this]() {
            m_fileTitle->clearFocus();
        });
        connect(m_fileTitle, &QLineEdit::editingFinished, this, [this]() {
            const QString t = m_fileTitle->text().trimmed();
            if (t.isEmpty()) { m_fileTitle->setText("Senza Titolo"); }
            if (!m_updating) emit fileRenamed(m_fileTitle->text());
        });
        nl->addWidget(m_fileTitle, 1);
        outer->addWidget(nameRow);
    }

    outer->addWidget(bandLine());

    // ── Layers (fixed header + scrollable embedded list) ─────────
    outer->addWidget(titleBand("Layers"));
    outer->addWidget(bandLine());

    m_layers = new LayersPanel(/*embedded*/ true);
    m_layers->setObjectName("layersEmbedded");
    // Reserve room for exactly four rows (row = 52 + 6 spacing), then scroll.
    const int fourRows = Ui::px(4 * 52 + 3 * 6 + 8);
    m_layers->setMinimumHeight(fourRows);
    m_layers->setMaximumHeight(fourRows);
    outer->addWidget(m_layers, 0);

    // ── Parameters (fixed header + scrollable adjustments) ───────
    outer->addWidget(bandLine());
    outer->addWidget(titleBand("Parameters"));
    outer->addWidget(bandLine());   // separates the title from the scrolling part

    m_adjust = new AdjustmentsPanel;
    connect(m_adjust, &AdjustmentsPanel::adjustmentsChanged,
            this, &ControlsPanel::adjustmentsChanged);
    connect(m_adjust, &AdjustmentsPanel::resetRequested,
            this, &ControlsPanel::resetRequested);
    outer->addWidget(m_adjust, 1);
}

void ControlsPanel::setFileName(const QString& name)
{
    m_updating = true;
    m_fileTitle->setText(name.isEmpty() ? "Senza Titolo" : name);
    m_fileTitle->setCursorPosition(0);
    m_updating = false;
}

// ── Parameters ───────────────────────────────────────────────

Adjustments ControlsPanel::adjustments() const          { return m_adjust->adjustments(); }
void ControlsPanel::setAdjustments(const Adjustments& a) { m_adjust->setAdjustments(a); }
void ControlsPanel::setSourceImage(const QImage& img)    { m_adjust->setSourceImage(img); }
