#pragma once
#include <QWidget>
#include <vector>
#include "core/symbol.h"

class SymbolSlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit SymbolSlotWidget(QWidget* parent = nullptr);
    void setSymbols(const std::vector<Symbol>& symbols);
signals:
    void symbolsChanged(const std::vector<Symbol>& symbols);
private:
    std::vector<Symbol> m_symbols;
};
