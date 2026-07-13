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

signals:
    void adjustmentsChanged();
    void resetRequested();

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
    SliderRow* m_blur             = nullptr;
    SliderRow* m_grain            = nullptr;

    // Resolution
    SliderRow* m_size = nullptr;

    // Creative
    SliderRow* m_posterize = nullptr;
    SliderRow* m_threshold = nullptr;
};
