#pragma once

#include "../core/Params.h"
#include "Widgets.h"
#include <QWidget>
#include <QImage>
#include <QColor>
#include <functional>

class HalftonePage;
class DitherPage;
class AsciiPage;
class TonalControlsWidget;
class FillSwatch;

/**
 * ModePanel (right column)
 *
 * Rectangle tab row (Halftone / Dither / Ascii) on top, then the active
 * mode's sections (Shape, Settings) followed by the shared Fill (palette),
 * Background and Export sections.
 *
 * The pages edit the active layer's settings; Fill/Background edit the
 * shared tonal + document background. Clicking a tab emits modeSelected().
 */
class ModePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ModePanel(QWidget* parent = nullptr);

    RenderMode       mode() const { return m_mode; }
    HalftoneSettings halftoneSettings() const;
    DitherSettings   ditherSettings()   const;
    AsciiSettings    asciiSettings()    const;

    // Fill (tonal palette) — moved here from the left column.
    TonalSettings tonalSettings() const;
    void          setTonalSettings(const TonalSettings& t);   // silent
    void          setColorsEnabled(bool enabled);
    void          setSourceImage(const QImage& img);

    // Background (shared document colour).
    QColor background()        const;
    float  backgroundOpacity() const;
    void   setBackground(QColor c, float opacity);            // silent

    QString outputFormat()   const;

    void setFromLayer(const Layer& layer);   // silent

signals:
    void paramsChanged();
    void tonalChanged();
    void backgroundChanged();
    void blendChanged(BlendMode mode);       // Fusion combo → active layer
    void modeSelected(RenderMode m);         // user clicked a tab
    void clearModeRequested();               // user clicked the "X" → back to Original
    void exportRequested();

private:
    void setMode(RenderMode m);   // silent: updates tabs + visible page

    QWidget*     m_fillSection = nullptr;   // Fill section (per-mode tonal)
    QPushButton* m_tabHalftone = nullptr;
    QPushButton* m_tabDither   = nullptr;
    QPushButton* m_tabAscii    = nullptr;

    HalftonePage* m_halftonePage = nullptr;
    DitherPage*   m_ditherPage   = nullptr;
    AsciiPage*    m_asciiPage    = nullptr;

    TonalControlsWidget* m_tonal       = nullptr; // Fill
    bool                 m_fillEnabled = true;    // Fill section open = fill present
    std::function<void(bool)> m_setFillOpen;      // collapse/expand the fill section
    FillSwatch*          m_bgSwatch  = nullptr;   // Background
    bool                 m_bgEnabled = true;      // Background section open = present
    std::function<void(bool)> m_setBgOpen;        // collapse/expand the bg section

    NoWheelComboBox* m_format     = nullptr;

    RenderMode m_mode     = RenderMode::Halftone;
    bool       m_updating = false;
};
