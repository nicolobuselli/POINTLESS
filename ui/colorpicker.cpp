#include "colorpicker.h"

ColorPicker::ColorPicker(QWidget* parent) : QWidget(parent) {
    // Inizializza con arancione di default
    m_color = QColor("#E05530");
    // TODO: implementazione UI
}

QColor ColorPicker::color() const {
    return m_color;
}

void ColorPicker::setColor(const QColor& c) {
    if (m_color != c) {
        m_color = c;
        emit colorChanged(m_color);
        update();
    }
}
