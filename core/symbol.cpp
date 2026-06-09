#include "symbol.h"
#include <QFile>
#include <QSvgRenderer>
#include <QPainter>

Symbol loadSymbolFromSvg(const QString& svgPath) {
    Symbol s;
    s.isSvg = true;
    QFile file(svgPath);
    if (file.open(QIODevice::ReadOnly)) {
        s.svgData = QString::fromUtf8(file.readAll());
        // Parsing SVG in QPainterPath opzionale
    }
    return s;
}

Symbol createBasicSymbol(const QString& name) {
    Symbol s;
    s.name = name;
    // Esempio: path = cerchio, quadrato, ecc.
    if (name == "circle") {
        s.path.addEllipse(QRectF(-0.5, -0.5, 1, 1));
    } else if (name == "square") {
        s.path.addRect(QRectF(-0.5, -0.5, 1, 1));
    }
    return s;
}
