#pragma once

#include "../core/Params.h"
#include "Widgets.h"
#include <QWidget>
#include <QVBoxLayout>
#include <functional>
#include <vector>

/**
 * TonalControlsWidget
 *
 * One dropdown decides the color strategy:
 *   "Image colors"      → sample colors from the picture
 *   "1 color".."5 color"→ map luminosity onto N tones
 * Each tone row = color swatch + level slider (where that tone sits
 * on the shadows→highlights axis). A palette dropdown loads presets;
 * editing anything flips it back to "Custom".
 */
class TonalControlsWidget : public QWidget
{
public:
    std::function<void()> onChanged;

    explicit TonalControlsWidget(const TonalSettings& initial,
                                 QWidget* parent = nullptr);

    TonalSettings settings() const { return m_settings; }
    void setSettings(const TonalSettings& s);   // silent

private:
    void rebuildRows();
    void syncModeCombo();
    void emitChanged();
    void openTonePicker(int idx, QWidget* anchor);
    static QStringList labelsFor(int n);

    TonalSettings    m_settings;
    NoWheelComboBox* m_modeCombo    = nullptr;
    NoWheelComboBox* m_paletteCombo = nullptr;
    QWidget*         m_paletteRow   = nullptr;
    QWidget*         m_rowsContainer = nullptr;
    QVBoxLayout*     m_rowsLayout    = nullptr;

    struct Row {
        FillSwatch*    swatch = nullptr;
        NoWheelSlider* slider = nullptr;
    };
    std::vector<Row> m_rows;

    bool m_updating = false;
};
