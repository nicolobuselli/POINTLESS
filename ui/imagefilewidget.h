#pragma once
#include <QWidget>
#include <QImage>
#include <QString>

class ImageFileWidget : public QWidget {
    Q_OBJECT
public:
    explicit ImageFileWidget(QWidget* parent = nullptr);
    QImage image() const;
    QString filePath() const;
signals:
    void imageLoaded(const QImage& img);
};
