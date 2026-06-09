#pragma once
#include <QMainWindow>
#include <QImage>
#include "previewwidget.h"
#include "colorpicker.h"
#include "symbolslotwidget.h"
#include "halftoneparamswidget.h"
#include "imagefilewidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    PreviewWidget* preview;
    ColorPicker* colorPicker;
    SymbolSlotWidget* symbolSlot;
    HalftoneParamsWidget* paramsWidget;
    ImageFileWidget* imageFileWidget;
    QImage loadedImage;
    QColor currentColor = QColor("#E05530");
    void setupUi();
    void updatePreview();
};
