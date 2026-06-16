#pragma once

#include "../core/Params.h"
#include <QWidget>
#include <QImage>
#include <QColor>

class AdjustmentsPanel;
class TonalControlsWidget;
class FillSwatch;
class QStackedWidget;
class QPushButton;

/**
 * ControlsPanel (left column)
 *
 * Two tabs share the column:
 *   Colors     → tonal controls (palette, tones, save, random)
 *   Parameters → image adjustments (Tone / Detail / Resolution / Creative)
 *
 * The shared Background color is pinned at the bottom. Like the right
 * ModePanel, the tabs edit the active layer's state; MainWindow injects
 * the active layer's tonal settings into the render struct of its kind.
 */
class ControlsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ControlsPanel(QWidget* parent = nullptr);

    // Parameters tab (image adjustments)
    Adjustments adjustments() const;
    void        setAdjustments(const Adjustments& a);   // silent
    void        setSourceImage(const QImage& img);

    // Colors tab (tonal controls)
    TonalSettings tonalSettings() const;
    void          setTonalSettings(const TonalSettings& t);   // silent
    void          setColorsEnabled(bool enabled);

    // Background (pinned bottom)
    QColor background()        const;
    float  backgroundOpacity() const;
    void   setBackground(QColor c, float opacity);   // silent

signals:
    void adjustmentsChanged();
    void tonalChanged();
    void backgroundChanged();
    void resetRequested();

private:
    void selectTab(int index);   // 0 = Colors, 1 = Parameters

    QPushButton*    m_tabColors = nullptr;
    QPushButton*    m_tabParams = nullptr;
    QStackedWidget* m_stack     = nullptr;

    TonalControlsWidget* m_tonal    = nullptr;
    AdjustmentsPanel*    m_adjust   = nullptr;
    FillSwatch*          m_bgSwatch = nullptr;

    bool m_updating = false;
};
