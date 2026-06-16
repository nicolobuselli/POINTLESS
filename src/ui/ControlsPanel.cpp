#include "ControlsPanel.h"
#include "AdjustmentsPanel.h"
#include "TonalControlsWidget.h"
#include "Widgets.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QScrollArea>
#include <QPushButton>

ControlsPanel::ControlsPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("sidePanel");
    setMinimumWidth(280);
    setMaximumWidth(520);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── Tabs (Colors / Parameters) ───────────────────────────
    {
        auto* tabRow = new QWidget;
        tabRow->setObjectName("tabRow");
        auto* tl = new QHBoxLayout(tabRow);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(0);

        auto makeTab = [&](const QString& text) {
            auto* b = new QPushButton(text);
            b->setObjectName("tabBtn");
            b->setCheckable(true);
            b->setAutoExclusive(true);
            b->setFixedHeight(42);
            b->setCursor(Qt::PointingHandCursor);
            tl->addWidget(b, 1);
            return b;
        };
        m_tabColors = makeTab("Colors");
        m_tabParams = makeTab("Parameters");
        m_tabColors->setProperty("tabPos", "first");
        m_tabParams->setProperty("tabPos", "last");
        m_tabColors->setChecked(true);

        connect(m_tabColors, &QPushButton::clicked, this, [this]() { selectTab(0); });
        connect(m_tabParams, &QPushButton::clicked, this, [this]() { selectTab(1); });

        outer->addWidget(tabRow);
    }

    // ── Stacked pages ────────────────────────────────────────
    m_stack = new QStackedWidget;

    // Page 0 — Colors (tonal controls in a scroll area)
    {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameShape(QFrame::NoFrame);

        auto* content = new QWidget;
        content->setObjectName("controlRoot");
        auto* cl = new QVBoxLayout(content);
        cl->setContentsMargins(16, 16, 16, 14);
        cl->setSpacing(0);

        m_tonal = new TonalControlsWidget(
            TonalSettings{ ToneMode::FixedTones, defaultAccentTones(1) });
        m_tonal->onChanged = [this]() { if (!m_updating) emit tonalChanged(); };
        cl->addWidget(new CollapsibleSection("Tonal controls", m_tonal));
        cl->addStretch();

        scroll->setWidget(content);
        m_stack->addWidget(scroll);
    }

    // Page 1 — Parameters (image adjustments)
    m_adjust = new AdjustmentsPanel;
    connect(m_adjust, &AdjustmentsPanel::adjustmentsChanged,
            this, &ControlsPanel::adjustmentsChanged);
    connect(m_adjust, &AdjustmentsPanel::resetRequested,
            this, &ControlsPanel::resetRequested);
    m_stack->addWidget(m_adjust);

    outer->addWidget(m_stack, 1);

    // ── Background (pinned bottom) ───────────────────────────
    {
        auto* bgBox = new QWidget;
        bgBox->setObjectName("exportBox");
        auto* bl = new QVBoxLayout(bgBox);
        bl->setContentsMargins(16, 12, 16, 14);
        bl->setSpacing(8);
        bl->addWidget(makeSectionTitle("Background"));

        m_bgSwatch = new FillSwatch(QColor(0x0A, 0x0A, 0x0A), 1.0f, /*showOpacity*/ true);
        m_bgSwatch->onColorEdited    = [this](QColor) { if (!m_updating) emit backgroundChanged(); };
        m_bgSwatch->onClicked = [this]() {
            auto* dlg = new ColorPickerDialog(m_bgSwatch->color(),
                                              m_bgSwatch->opacity(),
                                              /*showOpacity*/ true, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->moveNextTo(m_bgSwatch);
            dlg->onColorChanged = [this](QColor c, float a) {
                m_bgSwatch->setColor(c);
                m_bgSwatch->setOpacity(a);
                if (!m_updating) emit backgroundChanged();
            };
            dlg->show();
            dlg->raise();
            dlg->activateWindow();
        };
        bl->addWidget(m_bgSwatch);

        outer->addWidget(bgBox);
    }
}

void ControlsPanel::selectTab(int index)
{
    m_stack->setCurrentIndex(index);
    m_tabColors->setChecked(index == 0);
    m_tabParams->setChecked(index == 1);
}

// ── Parameters ───────────────────────────────────────────────

Adjustments ControlsPanel::adjustments() const         { return m_adjust->adjustments(); }
void ControlsPanel::setAdjustments(const Adjustments& a) { m_adjust->setAdjustments(a); }
void ControlsPanel::setSourceImage(const QImage& img)    { m_adjust->setSourceImage(img); m_tonal->setSourceImage(img); }

// ── Colors ───────────────────────────────────────────────────

TonalSettings ControlsPanel::tonalSettings() const { return m_tonal->settings(); }

void ControlsPanel::setTonalSettings(const TonalSettings& t)
{
    m_updating = true;
    m_tonal->setSettings(t);
    m_updating = false;
}

void ControlsPanel::setColorsEnabled(bool enabled)
{
    m_tonal->setEnabled(enabled);
}

// ── Background ───────────────────────────────────────────────

QColor ControlsPanel::background()        const { return m_bgSwatch->color(); }
float  ControlsPanel::backgroundOpacity() const { return m_bgSwatch->opacity(); }

void ControlsPanel::setBackground(QColor c, float opacity)
{
    m_updating = true;
    m_bgSwatch->setColor(c);
    m_bgSwatch->setOpacity(opacity);
    m_updating = false;
}
