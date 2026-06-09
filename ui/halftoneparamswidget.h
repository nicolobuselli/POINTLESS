#pragma once
#include <QWidget>
#include <QSlider>
#include <QCheckBox>
#include <QColor>

class HalftoneParamsWidget : public QWidget {
    Q_OBJECT
public:
    explicit HalftoneParamsWidget(QWidget* parent = nullptr);
    // Getter per i parametri
    int gridSize() const;
    double gamma() const;
    double jitter() const;
    double opacity() const;
    double symbolSize() const;
    bool enableStroke() const;
    double strokeWidth() const;
    QColor strokeColor() const;
    QColor fillColor() const;
signals:
    void paramsChanged();
private:
    QSlider* gridSlider;
    QSlider* gammaSlider;
    QSlider* jitterSlider;
    QSlider* opacitySlider;
    QSlider* symbolSizeSlider;
    QCheckBox* strokeCheck;
    QSlider* strokeWidthSlider;
    // ... altri controlli
};
