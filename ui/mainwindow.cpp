#include "mainwindow.h"
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setupUi();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi() {
    QWidget* central = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(central);
    preview = new PreviewWidget(this);
    colorPicker = new ColorPicker(this);
    symbolSlot = new SymbolSlotWidget(this);
    paramsWidget = new HalftoneParamsWidget(this);
    imageFileWidget = new ImageFileWidget(this);
    layout->addWidget(imageFileWidget);
    layout->addWidget(preview);
    layout->addWidget(colorPicker);
    layout->addWidget(paramsWidget);
    layout->addWidget(symbolSlot);

    connect(imageFileWidget, &ImageFileWidget::imageLoaded, this, [this](const QImage& img) {
        loadedImage = img;
        updatePreview();
    });
    setCentralWidget(central);
    setWindowTitle("ULTRA Ditherer");

    // Preview iniziale
    updatePreview();

    // Collega il color picker alla preview
    connect(colorPicker, &ColorPicker::colorChanged, this, [this](const QColor& c) {
        currentColor = c;
        updatePreview();
    });
}

void MainWindow::updatePreview() {
    QImage img(256, 256, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(currentColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRectF(28, 28, 200, 200));
    p.end();
    preview->setImage(img);
}
