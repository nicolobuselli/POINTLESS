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
#include <array>

// ============================================================
//  Shared custom widgets for ULTRA_Ditherer panels.
//  Callback style (std::function) — no extra signals/moc needed.
// ============================================================

// Drag-and-drop payload for a library source (a board media id), used when a
// filmstrip thumbnail is dragged onto the Layers panel or the preview canvas.
inline constexpr char kMediaMime[] = "application/x-ultraditherer-media";

class NoWheelSlider : public QSlider {
public:
    explicit NoWheelSlider(Qt::Orientation o, QWidget* p = nullptr) : QSlider(o, p) {
        setFocusPolicy(Qt::NoFocus);   // Tab skips sliders, stops only on the number boxes
    }
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
    void setCompact();   // half-height variant with smaller font
    void setTextLabel(const QString& text);   // bold letter (e.g. "W") instead of an icon

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void beginEdit();
    void commitEdit();
    void updateDisplay();
    QLabel*    m_iconLbl   = nullptr;
    QLineEdit* m_valueEdit = nullptr;
    int        m_min, m_max, m_value;
    QString    m_suffix;
    bool       m_compact      = false;
    bool       m_dragging     = false;
    QPoint     m_dragStart;
    int        m_dragStartVal = 0;
};

// ── LevelsWidget ─────────────────────────────────────────────
// Photoshop-style levels editor: gradient track with three draggable
// triangular handles (black · mid-gamma · white) and input boxes.
//
//  black : 0..255   — clips input shadows
//  mid   : 10..500  — midpoint gamma × 100  (100 = neutral / γ 1.0)
//  white : 0..255   — clips input highlights

class LevelsWidget : public QWidget {
public:
    std::function<void()> onChanged;

    explicit LevelsWidget(QWidget* parent = nullptr);

    int  blackPoint() const { return m_black; }
    int  midPoint()   const { return m_mid;   }
    int  whitePoint() const { return m_white; }
    void setValues(int black, int mid, int white);   // silent — no callback

    // Supply a 256-bin luminance histogram (counts, not normalised).
    // Pass an empty/zero array to clear.
    void setHistogram(const std::array<int,256>& h);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    enum class Handle { None, Black, Mid, White };

    float  valToX(int v) const;
    int    xToVal(float x) const;
    float  midFracFrom(int midV) const;
    float  midPixX() const;
    int    midValFromX(float x) const;
    Handle hitTest(QPoint p) const;
    void   drawHandle(QPainter& p, float x, Handle h, bool active);
    void   syncBoxes();

    int    m_black = 0, m_mid = 100, m_white = 255;
    Handle m_dragging       = Handle::None;
    QPoint m_dragStart;
    int    m_dragStartBlack = 0, m_dragStartMid = 100, m_dragStartWhite = 255;
    bool   m_updating       = false;

    std::array<int,256> m_histogram{};
    bool                m_hasHistogram = false;

    DragSpinBox* m_boxBlack = nullptr;
    DragSpinBox* m_boxMid   = nullptr;
    DragSpinBox* m_boxWhite = nullptr;
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
    QWidget*       m_header      = nullptr;
    QWidget*       m_content     = nullptr;
    QWidget*       m_contentWrap = nullptr;   // adds the standard column gutter
    QWidget*       m_extras      = nullptr;
    ChevronButton* m_chevron     = nullptr;
    bool           m_expanded = true;
};

// ── FillSwatch ───────────────────────────────────────────────
// Color chip + hex text; optional opacity readout (drag to change).

class FillSwatch : public QWidget {
public:
    std::function<void()>       onClicked;
    std::function<void(QColor)> onColorEdited;   // hex typed/pasted by hand

    FillSwatch(QColor color, float opacity, bool showOpacity = true,
               QWidget* parent = nullptr);

    QColor color()   const { return m_color; }
    float  opacity() const { return m_opacity; }
    void setColor(QColor c);
    void setOpacity(float op);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    int  dividerX() const;
    void placeHexEdit();
    void syncHexText();

    QColor     m_color;
    float      m_opacity;
    bool       m_showOpacity = true;
    QLineEdit* m_hexEdit     = nullptr;
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

    ColorFieldWidget*  m_field        = nullptr;
    HueBarWidget*      m_hueBar       = nullptr;
    OpacityBarWidget*  m_opBar        = nullptr;
    QLineEdit*         m_hexInput     = nullptr;
    QLineEdit*         m_opacityInput = nullptr;
    float m_h = 0.f, m_s = 1.f, m_v = 1.f, m_a = 1.f;
    QPoint m_dragOffset;
};

// ── Small helpers ────────────────────────────────────────────

QLabel*      makeParamLabel(const QString& text);
QLabel*      makeSectionTitle(const QString& text);
QFrame*      makeSeparatorLine();
QPushButton* makeIconButton(const QString& iconRes);

// Make a scroll area's vertical scrollbar auto-hide: it fades in only while
// the user is actually scrolling (and only exists when content overflows),
// then fades back out shortly after.
class QAbstractScrollArea;
void installAutoHideScrollbar(QAbstractScrollArea* area);

// Like installAutoHideScrollbar, but the scrollbar floats over the content
// (reserves no width), so content/dividers reach the panel's right edge.
void installOverlayScrollbar(QAbstractScrollArea* area);
