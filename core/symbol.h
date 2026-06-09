#pragma once
#include <QPainterPath>
#include <QString>

struct Symbol {
    QString name;
    QPainterPath path;
    double scale = 1.0;
    double aspect = 1.0;
    bool isSvg = false;
    QString svgData;
    // Altri parametri custom
};

Symbol loadSymbolFromSvg(const QString& svgPath);
Symbol createBasicSymbol(const QString& name);
