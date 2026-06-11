#pragma once

#include "../core/Params.h"
#include "Widgets.h"
#include <QWidget>
#include <QLineEdit>

/**
 * AdjustmentsPanel (left column)
 *
 * Global image adjustments on top, export controls pinned at the bottom.
 */
class AdjustmentsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AdjustmentsPanel(QWidget* parent = nullptr);

    Adjustments adjustments() const;
    void        setAdjustments(const Adjustments& a);   // silent

    QString outputFileName() const;
    QString outputFormat()   const;

signals:
    void adjustmentsChanged();
    void exportRequested();
    void resetRequested();

private:
    SliderRow* m_brightness = nullptr;
    SliderRow* m_contrast   = nullptr;
    SliderRow* m_saturation = nullptr;
    SliderRow* m_size       = nullptr;
    SliderRow* m_sharpenStrength = nullptr;
    SliderRow* m_sharpenRadius   = nullptr;
    SliderRow* m_noise   = nullptr;
    SliderRow* m_denoise = nullptr;
    SliderRow* m_blur    = nullptr;

    QLineEdit*       m_edtOutputName = nullptr;
    NoWheelComboBox* m_cmbFormat     = nullptr;
};
