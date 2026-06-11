#pragma once

#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QColor>
#include <functional>

// ============================================================
//  Shared custom widgets for ULTRA_Ditherer panels.
//  Callback style (std::function) — no extra signals/moc needed.
// ============================================================

class NoWheelSlider : public QSlider {
public:
    using QSlider::QSlider;
protected:
    void wheelEvent(QWheelEvent* e) override;
};

class NoWheelComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;
protected:
    void wheelEvent(QWheelEvent* e) override;
};

// ── DragSpinBox ──────────────────────────────────────────────
// Numeric box: drag horizontally to change, double-click to type.

class DragSpinBox : public QFrame {
public:
    std::function<void(int)> onValueChanged;

    DragSpinBox(const QString& iconRes, int minVal, int maxVal, int defVal,
                const QString& suffix = QString(), QWidget* parent = nullptr);

    int  value() const { return m_value; }
    void setValue(int v);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    void updateDisplay();
    QLabel*    m_iconLbl   = nullptr;
    QLabel*    m_valueLbl  = nullptr;
    QLineEdit* m_lineEdit  = nullptr;
    int        m_min, m_max, m_value;
    QString    m_suffix;
    bool       m_editingValue = false;
    bool       m_dragging     = false;
    QPoint     m_dragStart;
    int        m_dragStartVal = 0;
};

// ── SliderRow ────────────────────────────────────────────────
// Optional label on top, slider + numeric box side by side.

class SliderRow : public QWidget {
public:
    std::function<void(int)> onValueChanged;

    SliderRow(const QString& label, int minVal, int maxVal, int defVal,
              QWidget* parent = nullptr);

    int  value() const;
    void setValue(int v);                 // silent (no callback)

private:
    NoWheelSlider* m_slider = nullptr;
    DragSpinBox*   m_box    = nullptr;
    bool           m_updating = false;
};

// ── ChevronButton ────────────────────────────────────────────

class ChevronButton : public QPushButton {
public:
    enum Direction { Up, Down, Left, Right };

    explicit ChevronButton(Direction dir, QWidget* parent = nullptr);
    void setDirection(Direction dir);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    Direction m_dir;
};

// ── CollapsibleSection ───────────────────────────────────────
// Header (title + chevron) that toggles a content widget.

class CollapsibleSection : public QWidget {
public:
    CollapsibleSection(const QString& title, QWidget* content,
                       QWidget* parent = nullptr);

    void setExpanded(bool expanded);
    QWidget* headerExtras() const { return m_extras; }   // slot left of chevron

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    QWidget*       m_header  = nullptr;
    QWidget*       m_content = nullptr;
    QWidget*       m_extras  = nullptr;
    ChevronButton* m_chevron = nullptr;
    bool           m_expanded = true;
};

// ── FillSwatch ───────────────────────────────────────────────
// Color chip + hex text; optional opacity readout (drag to change).

class FillSwatch : public QWidget {
public:
    std::function<void()>      onClicked;
    std::function<void(float)> onOpacityDragged;

    FillSwatch(QColor color, float opacity, bool showOpacity = true,
               QWidget* parent = nullptr);

    QColor color()   const { return m_color; }
    float  opacity() const { return m_opacity; }
    void setColor(QColor c);
    void setOpacity(float op);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    int dividerX() const;

    QColor m_color;
    float  m_opacity;
    bool   m_showOpacity = true;
    bool   m_dragging    = false;
    QPoint m_pressPos;
    float  m_dragStartOp = 1.f;
};

// ── ColorPickerDialog ────────────────────────────────────────

class ColorFieldWidget;
class HueBarWidget;
class OpacityBarWidget;

class ColorPickerDialog : public QDialog {
public:
    std::function<void(QColor, float)> onColorChanged;

    ColorPickerDialog(QColor initial, float initialOpacity,
                      bool showOpacity = true, QWidget* parent = nullptr);

    QColor selectedColor()   const;
    float  selectedOpacity() const { return m_a; }

    // Position the dialog next to an anchor widget, kept on screen.
    void moveNextTo(QWidget* anchor);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void buildUI(bool showOpacity);
    void updateAllFromHSV();
    void updatePreviewAndHex();

    ColorFieldWidget*  m_field    = nullptr;
    HueBarWidget*      m_hueBar   = nullptr;
    OpacityBarWidget*  m_opBar    = nullptr;
    QLabel*            m_preview  = nullptr;
    QLineEdit*         m_hexInput = nullptr;
    float m_h = 0.f, m_s = 1.f, m_v = 1.f, m_a = 1.f;
    QPoint m_dragOffset;
};

// ── Small helpers ────────────────────────────────────────────

QLabel*      makeParamLabel(const QString& text);
QLabel*      makeSectionTitle(const QString& text);
QFrame*      makeSeparatorLine();
QPushButton* makeIconButton(const QString& iconRes);
