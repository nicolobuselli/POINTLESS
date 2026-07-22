#pragma once

#include "../core/Params.h"
#include "../core/AnimParams.h"
#include "Widgets.h"
#include <QWidget>
#include <QLineEdit>
#include <QImage>
#include <QSet>
#include <QHash>

class QPushButton;
class QVBoxLayout;
class QLabel;
class QScrollArea;
class CheckSquare;

/**
 * AdjustmentsPanel (left column)
 *
 * Image preprocessing controls organised into four collapsible sections:
 *   Tone · Detail · Resolution · Creative
 *
 * Export controls are pinned at the bottom.
 */
class AdjustmentsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AdjustmentsPanel(QWidget* parent = nullptr);

    Adjustments adjustments() const;
    void        setAdjustments(const Adjustments& a);   // silent

    // Compute luminance histogram from source image and push to Levels widget.
    void setSourceImage(const QImage& img);

    // Tint the label of every visible row whose ParamId is in the set (has a
    // keyframe track on the active layer). Rows with no visible control
    // (removed-from-UI Adjustments fields) are silently skipped.
    void setAnimatedParams(const QSet<ParamId>& ids);

    // Row widget → ParamId, for hover-to-keyframe ("I" key). Same rows as
    // setAnimatedParams touches.
    QHash<QWidget*, ParamId> paramWidgets() const;

    // Round-trip the "Localize" button's checked state (silent — no signal).
    void setLocalizeChecked(bool on);

    // Inserts a widget above every row here (Brightness, Contrast, …), inside
    // the same scrolling viewport — used by ControlsPanel to fold the
    // Image Adjustments (Position/Rotation/Scale) rows into the same scroll as the
    // adjustments, so the merged "Image Adjustments" section scrolls as one list
    // instead of Image Adjustments staying pinned above an independently-scrolling
    // Adjustments viewport.
    void prependWidget(QWidget* w);

    // Scrolls the internal viewport back to its top row — called whenever
    // the active layer/mode changes, so the column always starts flush with
    // its title instead of keeping whatever offset the previous layer left.
    void scrollToTop();

signals:
    void adjustmentsChanged();
    void resetRequested();
    // The whole-layer mask circle (position/radius/falloff, on-canvas) should
    // flip enabled/disabled. MainWindow owns which LocParam that maps to for
    // the active layer's kind.
    void localizeToggleRequested();

private:
    // Tone
    SliderRow* m_brightness  = nullptr;
    SliderRow* m_contrast    = nullptr;
    SliderRow* m_gamma       = nullptr;
    LevelsWidget* m_levels = nullptr;
    SliderRow* m_saturation  = nullptr;

    // Detail
    SliderRow* m_sharpenStrength  = nullptr;
    SliderRow* m_sharpenRadius    = nullptr;
    SliderRow* m_edgeEnhancement  = nullptr;
    QPushButton* m_invert         = nullptr;
    CheckSquare* m_invertSquare   = nullptr;   // checkmark indicator, see setAdjustments()
    SliderRow* m_blur             = nullptr;
    SliderRow* m_grain            = nullptr;

    // Resolution
    SliderRow* m_size = nullptr;

    // Creative
    SliderRow* m_posterize = nullptr;
    SliderRow* m_threshold = nullptr;

    QPushButton* m_localize       = nullptr;
    CheckSquare* m_localizeSquare = nullptr;   // checkmark indicator, see setLocalizeChecked()

    QVBoxLayout* m_vlay   = nullptr;   // scrollable content layout — see prependWidget()
    QScrollArea* m_scroll = nullptr;   // see scrollToTop()
};
