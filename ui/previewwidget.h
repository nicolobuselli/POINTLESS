#pragma once
#include <QWidget>
#include <QImage>

class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void setImage(const QImage& img);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QImage image;
};
