#pragma once
#include <QImage>
#include <QString>

namespace ImageUtils {
    QImage loadImage(const QString& path);
    QImage toGrayscale(const QImage& img);
    QImage resizeImage(const QImage& img, int w, int h);
}
