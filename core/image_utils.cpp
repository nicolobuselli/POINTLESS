#include "image_utils.h"
#include <QImageReader>

QImage ImageUtils::loadImage(const QString& path) {
    QImageReader reader(path);
    return reader.read();
}

QImage ImageUtils::toGrayscale(const QImage& img) {
    return img.convertToFormat(QImage::Format_Grayscale8);
}

QImage ImageUtils::resizeImage(const QImage& img, int w, int h) {
    return img.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
