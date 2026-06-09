#pragma once

#include <QScrollArea>
#include <QWidget>
#include <QComboBox>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QGroupBox>
#include <array>
#include "../core/HalftoneParams.h"

/**
 * ControlPanel
 *
 * Scrollable right panel containing all halftone parameter controls.
 * Emits paramsChanged() whenever any control is modified.
 * Emits exportRequested() when the Export button is clicked.
 * Emits fileRequested()   when Choose File is clicked.
 */
class ControlPanel : public QScrollArea
{
    Q_OBJECT

public:
    explicit ControlPanel(QWidget* parent = nullptr);

    HalftoneParams currentParams() const;
    QString        outputFileName()  const;
    QString        outputFormat()    const;

signals:
    void paramsChanged();
    void exportRequested();
    void fileRequested();

private:
    void buildLayout();
    void connectSignals();

    QWidget* buildSectionHeader(const QString& title);
    QFrame*  buildSeparator();

    // Helper: creates Label + QSlider + QDoubleSpinBox in a row, returns slider
    QWidget* makeFloatRow(const QString& label,
                          QSlider** sliderOut, QDoubleSpinBox** spinOut,
                          double minVal, double maxVal, double defVal, int sliderScale = 100);

    QWidget* makeIntRow(const QString& label,
                        QSlider** sliderOut, QSpinBox** spinOut,
                        int minVal, int maxVal, int defVal);

    // Sync helpers
    static void syncSliderToSpin(QSlider* s, QDoubleSpinBox* spin, int scale);
    static void syncSpinToSlider(QSlider* s, QDoubleSpinBox* spin, int scale);

    // --- LOAD FILE ---
    QPushButton* m_btnChooseFile;
    QLabel*      m_lblFilePath;

    // --- HALFTONE SHAPE ---
    QComboBox*   m_cmbShape;
    QWidget*     m_wgtSvgRow;
    QPushButton* m_btnLoadSvg;
    QLabel*      m_lblSvgPath;
    QString      m_svgPath;

    // --- PARAMETERS ---
    QSlider*       m_sldGridSize;
    QSpinBox*      m_spnGridSize;
    QSlider*       m_sldGamma;
    QDoubleSpinBox* m_spnGamma;
    QSlider*       m_sldJitter;
    QDoubleSpinBox* m_spnJitter;
    QSlider*       m_sldOpacity;
    QDoubleSpinBox* m_spnOpacity;
    QSlider*       m_sldSymbolSize;
    QDoubleSpinBox* m_spnSymbolSize;

    // --- STROKE ---
    QCheckBox*     m_chkStroke;
    QSlider*       m_sldStrokeWidth;
    QDoubleSpinBox* m_spnStrokeWidth;
    QSlider*       m_sldStrokeRadius;
    QDoubleSpinBox* m_spnStrokeRadius;
    QPushButton*   m_btnStrokeColor;
    QLabel*        m_lblStrokeColor;
    QColor         m_strokeColor { Qt::black };

    // --- COLOR ---
    QPushButton*   m_btnFillColor;
    QLabel*        m_lblFillColor;
    QColor         m_fillColor { Qt::black };
    QCheckBox*     m_chkUseImageColors;

    // --- MULTI-SYMBOL ---
    QCheckBox*     m_chkMultiSymbol;
    QWidget*       m_wgtMultiSymbol;   // container shown/hidden

    struct SymbolSlotUI {
        QPushButton*   btnLoad;
        QLabel*        lblName;
        QComboBox*     cmbShape;
        QSlider*       sldThreshold;
        QSpinBox*      spnThreshold;
        QString        svgPath;
    };
    std::array<SymbolSlotUI, 4> m_slots;

    // --- EXPORT ---
    QPushButton*   m_btnExport;
    QComboBox*     m_cmbFormat;
    QLineEdit*     m_edtOutputName;

    // Block recursive signals during init
    bool m_initializing = true;
};
