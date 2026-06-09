#include "halftoneparamswidget.h"
#include <QVBoxLayout>
#include <QLabel>

HalftoneParamsWidget::HalftoneParamsWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    gridSlider = new QSlider(Qt::Horizontal, this);
    gridSlider->setRange(2, 100);
    gridSlider->setValue(20);
    layout->addWidget(new QLabel("Grid Size"));
    layout->addWidget(gridSlider);
    gammaSlider = new QSlider(Qt::Horizontal, this);
    gammaSlider->setRange(1, 300);
    gammaSlider->setValue(100);
    layout->addWidget(new QLabel("Gamma"));
    layout->addWidget(gammaSlider);
    jitterSlider = new QSlider(Qt::Horizontal, this);
    jitterSlider->setRange(0, 100);
    jitterSlider->setValue(0);
    layout->addWidget(new QLabel("Jitter"));
    layout->addWidget(jitterSlider);
    opacitySlider = new QSlider(Qt::Horizontal, this);
    opacitySlider->setRange(0, 100);
    opacitySlider->setValue(100);
    layout->addWidget(new QLabel("Opacity"));
    layout->addWidget(opacitySlider);
    symbolSizeSlider = new QSlider(Qt::Horizontal, this);
    symbolSizeSlider->setRange(1, 200);
    symbolSizeSlider->setValue(100);
    layout->addWidget(new QLabel("Symbol Size"));
    layout->addWidget(symbolSizeSlider);
    strokeCheck = new QCheckBox("Enable Stroke", this);
    layout->addWidget(strokeCheck);
    strokeWidthSlider = new QSlider(Qt::Horizontal, this);
    strokeWidthSlider->setRange(1, 20);
    strokeWidthSlider->setValue(1);
    layout->addWidget(new QLabel("Stroke Width"));
    layout->addWidget(strokeWidthSlider);
    // ... altri controlli
    connect(gridSlider, &QSlider::valueChanged, this, &HalftoneParamsWidget::paramsChanged);
    connect(gammaSlider, &QSlider::valueChanged, this, &HalftoneParamsWidget::paramsChanged);
    connect(jitterSlider, &QSlider::valueChanged, this, &HalftoneParamsWidget::paramsChanged);
    connect(opacitySlider, &QSlider::valueChanged, this, &HalftoneParamsWidget::paramsChanged);
    connect(symbolSizeSlider, &QSlider::valueChanged, this, &HalftoneParamsWidget::paramsChanged);
    connect(strokeCheck, &QCheckBox::stateChanged, this, &HalftoneParamsWidget::paramsChanged);
    connect(strokeWidthSlider, &QSlider::valueChanged, this, &HalftoneParamsWidget::paramsChanged);
}

int HalftoneParamsWidget::gridSize() const { return gridSlider->value(); }
double HalftoneParamsWidget::gamma() const { return gammaSlider->value() / 100.0; }
double HalftoneParamsWidget::jitter() const { return jitterSlider->value() / 100.0; }
double HalftoneParamsWidget::opacity() const { return opacitySlider->value() / 100.0; }
double HalftoneParamsWidget::symbolSize() const { return symbolSizeSlider->value() / 100.0; }
bool HalftoneParamsWidget::enableStroke() const { return strokeCheck->isChecked(); }
double HalftoneParamsWidget::strokeWidth() const { return strokeWidthSlider->value(); }
QColor HalftoneParamsWidget::strokeColor() const { return Qt::black; /* placeholder */ }
QColor HalftoneParamsWidget::fillColor() const { return Qt::white; /* placeholder */ }
