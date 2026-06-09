#include "previewwidget.h"
#include <QPainter>

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {}

void PreviewWidget::setImage(const QImage& img) {
    image = img;
    update();
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if (!image.isNull()) {
        p.drawImage(rect(), image);
    }
}
