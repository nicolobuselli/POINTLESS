#pragma once
#include <QWidget>
#include <QColor>

class ColorPicker : public QWidget {
    Q_OBJECT
public:
    explicit ColorPicker(QWidget* parent = nullptr);
    QColor color() const;
    void setColor(const QColor& c);
signals:
    void colorChanged(const QColor& color);
private:
    QColor m_color = QColor("#E05530"); // Arancione di default
};
