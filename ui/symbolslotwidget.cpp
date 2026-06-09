#include "symbolslotwidget.h"

SymbolSlotWidget::SymbolSlotWidget(QWidget* parent) : QWidget(parent) {}

void SymbolSlotWidget::setSymbols(const std::vector<Symbol>& symbols) {
    m_symbols = symbols;
    emit symbolsChanged(m_symbols);
    update();
}
