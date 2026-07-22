#pragma once

#include "../core/Params.h"
#include "../core/AnimParams.h"
#include "Widgets.h"
#include <QWidget>
#include <QImage>
#include <QColor>
#include <QSet>
#include <QHash>
#include <functional>

class DotGridPage;
class DitherPage;
class AsciiPage;
class MosaicPage;
class HalftonePage;
class TonalControlsWidget;
class FillSwatch;
class QScrollArea;

/**
 * ModePanel (right column)
 *
 * Mode picker (Dot Grid / Dither / Ascii / Mosaic / Halftone) on top, then the active
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
    DotGridSettings  dotGridSettings()  const;
    DitherSettings   ditherSettings()   const;
    AsciiSettings    asciiSettings()    const;
    MosaicSettings   mosaicSettings()   const;
    HalftoneSettings halftoneSettings() const;

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
    void setMode(RenderMode m);              // silent: updates picker + visible page

    // Localization points: kept in sync with the Layer so the settings()
    // getters round-trip them faithfully (they are edited on-canvas).
    void setLocPoint(LocParam p, const LocPoint& pt);   // silent

    // Tint the label of every visible mode-page row whose ParamId is in the
    // set (has a keyframe track on the active layer).
    void setAnimatedParams(const QSet<ParamId>& ids);

    // Active mode page's control widgets → ParamId, for hover-to-keyframe.
    QHash<QWidget*, ParamId> paramWidgets() const;

    // Scrolls the section stack back to its top row — called whenever the
    // active layer/mode changes, so the column always starts flush with its
    // title instead of keeping whatever offset the previous mode left.
    void scrollToTop();

signals:
    void paramsChanged();
    void tonalChanged();
    void backgroundChanged();
    void blendChanged(BlendMode mode);       // Fusion combo → active layer
    void noModeOpacityChanged(float opacity);   // Opacity box in the no-mode Appearance section
    void modeSelected(RenderMode m);         // user clicked a tab
    void clearModeRequested();               // user clicked the "X" → back to Original
    void exportRequested();
    void localizationToggleRequested(LocParam p);   // a gutter loc dot was clicked

private:

    QWidget*     m_fillSection = nullptr;   // Fill section (per-mode tonal)
    QWidget*     m_noModeSection = nullptr;    // Appearance shown only when no mode is picked
    PopupPicker* m_noModeFusion  = nullptr;
    DragSpinBox* m_noModeOpacity = nullptr;
    QWidget*     m_mosaicTextsSection = nullptr;   // Texts section (Mosaic only, between Fill and Background)
    PopupPicker* m_modePick = nullptr;   // mode dropdown (replaced the tab row)
    QScrollArea* m_scroll   = nullptr;   // section stack viewport — see scrollToTop()

    DotGridPage*  m_dotGridPage  = nullptr;
    DitherPage*   m_ditherPage   = nullptr;
    AsciiPage*    m_asciiPage    = nullptr;
    MosaicPage*   m_mosaicPage   = nullptr;
    HalftonePage* m_halftonePage = nullptr;

    TonalControlsWidget* m_tonal       = nullptr; // Fill
    bool                 m_fillEnabled = true;    // Fill section open = fill present
    std::function<void(bool)> m_setFillOpen;      // collapse/expand the fill section
    FillSwatch*          m_bgSwatch  = nullptr;   // Background
    bool                 m_bgEnabled = true;      // Background section open = present
    std::function<void(bool)> m_setBgOpen;        // collapse/expand the bg section

    PopupPicker* m_format     = nullptr;

    RenderMode m_mode     = RenderMode::DotGrid;
    bool       m_updating = false;
};
