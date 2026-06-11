#pragma once

#include <QScrollArea>
#include <QWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFrame>
#include <QColor>
#include <QVector>
#include <QImage>
#include "../core/HalftoneParams.h"

class ControlPanel : public QScrollArea
{
    Q_OBJECT

public:
    explicit ControlPanel(QWidget* parent = nullptr);

    HalftoneParams currentParams() const;
    QString        outputFileName()  const;
    QString        outputFormat()    const;
    void           setSourcePreview(const QImage& img);

signals:
    void paramsChanged();
    void exportRequested();
    void fileRequested();

private:
    void buildLayout();

    QWidget*     makeSectionHeader(const QString& title, QPushButton** plusOut = nullptr);
    QFrame*      makeSeparator();
    QPushButton* makeIconButton(const QString& iconRes);
    QWidget*     makeSliderCol(const QString& lbl, QSlider** sldOut,
                               int minVal, int maxVal, int defVal);
    QWidget*     make2ColSliders(
                    const QString& lbl1, QSlider** sld1, int min1, int max1, int def1,
                    const QString& lbl2, QSlider** sld2, int min2, int max2, int def2);

    // ── Source image ──────────────────────────────────────────────
    QLabel*      m_sourcePreview;

    // ── Halftone shape ────────────────────────────────────────────
    QWidget*     m_shapesContainer;
    QVBoxLayout* m_shapesLayout;
    QWidget*     m_thresholdRow;
    QSlider*     m_sldThreshold;

    struct ShapeSlot {
        QWidget*     widget   = nullptr;
        QComboBox*   combo    = nullptr;
        QPushButton* minusBtn = nullptr;
        QWidget*     svgRow   = nullptr;
        QPushButton* svgBtn   = nullptr;
        QString      svgPath;
    };
    QVector<ShapeSlot> m_shapeSlots;

    void addShapeSlot(HalftoneShape shape = HalftoneShape::Circle, const QString& svgPath = {});
    void removeShapeSlot(QWidget* slotWidget);
    void refreshThresholdVisibility();

    // ── Parameters ────────────────────────────────────────────────
    QSlider*     m_sldGrid;
    QSlider*     m_sldGamma;
    QSlider*     m_sldJitter;
    QSlider*     m_sldSize;
    QFrame*      m_dsbOpacity;
    QFrame*      m_dsbCornerRadius;

    // ── Fill ──────────────────────────────────────────────────────
    bool         m_useImageColors = false;
    QPushButton* m_useImageColorsBtn = nullptr;
    QPushButton* m_fillPlusBtn       = nullptr;
    QWidget*     m_fillsContainer;
    QVBoxLayout* m_fillsLayout;

    struct FillSlot {
        QWidget*     widget   = nullptr;
        QWidget*     swatch   = nullptr;   // actual type: FillSwatch (defined in .cpp)
        QPushButton* minusBtn = nullptr;
    };
    QVector<FillSlot> m_fillSlots;

    void addFillSlot(QColor color = QColor(0xD9, 0xD9, 0xD9), float opacity = 1.0f);
    void removeFillSlot(QWidget* slotWidget);
    void openColorPicker(int idx);

    // ── Stroke ────────────────────────────────────────────────────
    bool         m_strokeEnabled = false;
    QWidget*     m_strokeContent;
    QPushButton* m_strokeToggleBtn;
    QSlider*     m_sldStrokeWidth;
    QPushButton* m_strokeColorBtn;
    QColor       m_strokeColor = Qt::black;

    void updateStrokeUI();
    void updateStrokeColorBtn();

    // ── Export ────────────────────────────────────────────────────
    QLineEdit*   m_edtOutputName;
    QComboBox*   m_cmbFormat;

    bool m_initializing = true;
};
