#pragma once

#include "../core/Params.h"
#include "Widgets.h"
#include <QWidget>
#include <QLineEdit>
#include <QImage>

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
    SliderRow* m_blur             = nullptr;
    SliderRow* m_grain            = nullptr;

    // Resolution
    SliderRow* m_size = nullptr;

    // Creative
    SliderRow* m_posterize = nullptr;
    SliderRow* m_threshold = nullptr;
};
