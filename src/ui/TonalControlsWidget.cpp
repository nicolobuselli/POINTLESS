#include "TonalControlsWidget.h"

#include <QHBoxLayout>
#include <QLabel>

namespace {
constexpr int kMaxTones = 5;
}

TonalControlsWidget::TonalControlsWidget(const TonalSettings& initial, QWidget* parent)
    : QWidget(parent)
    , m_settings(initial)
{
    if (m_settings.tones.empty())
        m_settings.tones = defaultTones(1);

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    // Mode dropdown
    m_modeCombo = new NoWheelComboBox;
    m_modeCombo->addItem("Image colors");
    for (int n = 1; n <= kMaxTones; ++n)
        m_modeCombo->addItem(QString::number(n) + (n == 1 ? " color" : " colors"));
    vl->addWidget(m_modeCombo);

    // Palette dropdown
    m_paletteRow = new QWidget;
    {
        auto* pl = new QVBoxLayout(m_paletteRow);
        pl->setContentsMargins(0, 0, 0, 0);
        pl->setSpacing(4);
        pl->addWidget(makeParamLabel("Palette"));
        m_paletteCombo = new NoWheelComboBox;
        m_paletteCombo->addItem("Custom");
        for (const auto& preset : palettePresets())
            m_paletteCombo->addItem(preset.name);
        pl->addWidget(m_paletteCombo);
    }
    vl->addWidget(m_paletteRow);

    // Tone rows
    m_rowsContainer = new QWidget;
    m_rowsLayout = new QVBoxLayout(m_rowsContainer);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(8);
    vl->addWidget(m_rowsContainer);

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (m_updating) return;
        if (idx == 0) {
            m_settings.mode = ToneMode::ImageColors;
        } else {
            m_settings.mode = ToneMode::FixedTones;
            const int n = idx;
            // Preserve existing colors, re-space the levels evenly.
            std::vector<ToneEntry> fresh = defaultTones(n);
            for (int i = 0; i < n && i < int(m_settings.tones.size()); ++i)
                fresh[i].color = m_settings.tones[i].color;
            m_settings.tones = fresh;
        }
        m_updating = true;
        m_paletteCombo->setCurrentIndex(0);
        m_updating = false;
        rebuildRows();
        emitChanged();
    });

    connect(m_paletteCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (m_updating || idx <= 0) return;
        const auto& presets = palettePresets();
        if (idx - 1 >= int(presets.size())) return;
        m_settings.mode  = ToneMode::FixedTones;
        m_settings.tones = tonesFromColors(presets[idx - 1].colors);
        m_updating = true;
        syncModeCombo();
        m_updating = false;
        rebuildRows();
        emitChanged();
    });

    syncModeCombo();
    rebuildRows();
}

void TonalControlsWidget::setSettings(const TonalSettings& s)
{
    m_settings = s;
    if (m_settings.tones.empty())
        m_settings.tones = defaultTones(1);
    if (int(m_settings.tones.size()) > kMaxTones)
        m_settings.tones.resize(kMaxTones);

    m_updating = true;
    syncModeCombo();
    m_paletteCombo->setCurrentIndex(0);
    m_updating = false;
    rebuildRows();
}

void TonalControlsWidget::syncModeCombo()
{
    const bool prev = m_updating;
    m_updating = true;
    if (m_settings.mode == ToneMode::ImageColors)
        m_modeCombo->setCurrentIndex(0);
    else
        m_modeCombo->setCurrentIndex(qBound(1, int(m_settings.tones.size()), kMaxTones));
    m_updating = prev;
}

QStringList TonalControlsWidget::labelsFor(int n)
{
    switch (n) {
        case 1:  return { "Color" };
        case 2:  return { "Highlights", "Shadows" };
        case 3:  return { "Highlights", "Midtones", "Shadows" };
        case 4:  return { "Highlights", "Light midtones", "Dark midtones", "Shadows" };
        default: return { "Highlights", "Light midtones", "Midtones", "Dark midtones", "Shadows" };
    }
}

void TonalControlsWidget::rebuildRows()
{
    // Clear previous rows
    m_rows.clear();
    while (QLayoutItem* item = m_rowsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    const bool fixed = (m_settings.mode == ToneMode::FixedTones);
    m_paletteRow->setVisible(fixed);
    m_rowsContainer->setVisible(fixed);
    if (!fixed) return;

    const int n = int(m_settings.tones.size());
    const QStringList labels = labelsFor(n);

    for (int i = 0; i < n; ++i) {
        auto* rowWidget = new QWidget;
        auto* vl = new QVBoxLayout(rowWidget);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(4);
        vl->addWidget(makeParamLabel(i < labels.size() ? labels[i] : QString("Tone %1").arg(i + 1)));

        auto* hl = new QHBoxLayout;
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(10);

        auto* swatch = new FillSwatch(m_settings.tones[i].color, 1.0f, /*showOpacity*/ false);
        swatch->setFixedWidth(96);

        auto* slider = new NoWheelSlider(Qt::Horizontal);
        slider->setRange(0, 255);
        slider->setValue(m_settings.tones[i].level);

        hl->addWidget(swatch);
        hl->addWidget(slider, 1);
        vl->addLayout(hl);

        m_rowsLayout->addWidget(rowWidget);
        m_rows.push_back({ swatch, slider });

        swatch->onClicked = [this, i, swatch]() { openTonePicker(i, swatch); };
        connect(slider, &QSlider::valueChanged, this, [this, i](int v) {
            if (m_updating) return;
            if (i < int(m_settings.tones.size())) {
                m_settings.tones[i].level = v;
                m_updating = true;
                m_paletteCombo->setCurrentIndex(0);
                m_updating = false;
                emitChanged();
            }
        });
    }
}

void TonalControlsWidget::openTonePicker(int idx, QWidget* anchor)
{
    if (idx < 0 || idx >= int(m_settings.tones.size())) return;
    auto* dlg = new ColorPickerDialog(m_settings.tones[idx].color,
                                      1.0f,
                                      /*showOpacity*/ false,
                                      this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->moveNextTo(anchor);

    dlg->onColorChanged = [this, idx](QColor c, float) {
        if (idx >= int(m_settings.tones.size())) return;
        m_settings.tones[idx].color = c;
        if (idx < int(m_rows.size())) m_rows[idx].swatch->setColor(c);
        m_updating = true;
        m_paletteCombo->setCurrentIndex(0);
        m_updating = false;
        emitChanged();
    };

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void TonalControlsWidget::emitChanged()
{
    if (!m_updating && onChanged) onChanged();
}
