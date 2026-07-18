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
#include <QVariant>
#include <QVector>
#include <QElapsedTimer>
#include <functional>
#include <array>

class QResizeEvent;

// ============================================================
//  Shared custom widgets for ULTRATOOL panels.
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
    void setRange(int minVal, int maxVal);    // re-clamps the current value
    void setTextLabel(const QString& text);   // bold letter (e.g. "W") instead of an icon

    // Tints the box's stroke to flag "this parameter has a keyframe track".
    void setAnimated(bool on);

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
    void setRange(int minVal, int maxVal); // silent; re-clamps the current value

    // Tints the value box's stroke to flag "this parameter has a keyframe
    // track" (timeline auto-key orange).
    void setAnimated(bool on);

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
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

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

// ── AnimProgressDialog ───────────────────────────────────────
// Frameless, dark-themed replacement for QProgressDialog (used by playback
// pre-render and frame export), styled like ColorPickerDialog instead of the
// native OS chrome. Same call pattern as QProgressDialog: construct, call
// setValue() in a loop, check wasCanceled().

class QProgressBar;

class AnimProgressDialog : public QDialog {
public:
    AnimProgressDialog(const QString& labelText, int maxValue, QWidget* parent = nullptr);

    void setMinimumDuration(int ms) { m_minDurationMs = ms; }
    void setValue(int v);
    void setLabelText(const QString& text);
    void setRange(int lo, int hi);   // (0,0) = indeterminate/busy
    bool wasCanceled() const { return m_canceled; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QLabel*       m_label      = nullptr;
    QProgressBar* m_bar        = nullptr;
    QElapsedTimer m_elapsed;
    int  m_minDurationMs = 0;
    bool m_canceled      = false;
};

// ── UnsavedChangesDialog ─────────────────────────────────────
// Frameless close-confirmation prompt, styled like the rest of the app
// instead of a native QMessageBox: mini title bar (ULTRATOOL + a
// permanently-red close button) over a warning icon, message, and Yes/No.
// The close button (or Esc) leaves choice() at Cancel — same as dismissing
// a native dialog without picking an option.

class UnsavedChangesDialog : public QDialog {
public:
    enum Choice { Cancel, Save, Discard };

    explicit UnsavedChangesDialog(const QString& documentName, QWidget* parent = nullptr);

    Choice choice() const { return m_choice; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Choice m_choice = Cancel;
};

// ── StyledMessageBox ─────────────────────────────────────────
// In-app-styled stand-in for QMessageBox: same frameless chrome as
// UnsavedChangesDialog (mini "ULTRATOOL" title bar + warning icon)
// instead of a native OS dialog. Two buttons (Yes/No) or one (OK),
// picked by leaving noText empty. Use askYesNo()/showMessage() below
// rather than constructing this directly.
class StyledMessageBox : public QDialog {
public:
    StyledMessageBox(const QString& message, QWidget* parent,
                      const QString& yesText, const QString& noText, bool defaultYes);

    bool accepted() const { return m_accepted; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    bool m_accepted = false;
};

// Blocking Yes/No prompt, styled like the app instead of a native QMessageBox.
// Returns true iff the user picked "Yes" (closing via the X or Esc is "No").
bool askYesNo(QWidget* parent, const QString& message, bool defaultYes = true);

// Blocking single-button ("OK") notice, same styled chrome.
void showMessage(QWidget* parent, const QString& message);

// ── PopupPicker ──────────────────────────────────────────────
// Combo-look button that opens a boxed grid popup of value entries, with
// optional pill group headers (used for Algorithm, Shape, Grid, Fusion,
// Charset, Font…). The popup opens to the left of the ancestor named
// "sidePanel" (the right mode column), top-aligned with the button, so long
// option names/lists have room over the canvas instead of squeezing into the
// narrow column. Scrolls internally if the entry list doesn't fit on screen.

struct PopupPickerEntry {
    QVariant value;      // selectable value; ignored for headers
    QString  label;      // cell text; empty marks this entry as a header
    QString  header;     // header pill text (used when label is empty and !lineHeader)
    QString  tooltip;    // optional per-cell tooltip
    bool     lineHeader = false;   // header renders as a plain divider line, no text/pill
};

class PopupPicker : public QPushButton {
public:
    std::function<void(QVariant)> onSelected;   // user pick only, not setValue

    explicit PopupPicker(int columns = 2, QWidget* parent = nullptr);

    void setEntries(const QVector<PopupPickerEntry>& entries);
    QVariant value() const { return m_value; }
    void setValue(const QVariant& v);   // silent — no onSelected
    void setPlaceholder(const QString& text);   // no entry selected (value matches nothing)

    // Orange/white CTA look (same chrome as #accentBtn) instead of the
    // standard dark box — used for the mode picker, which must stay
    // unmistakable at a glance.
    void setAccent(bool on);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void setArrowOpen(bool open);
    void openPopup();
    void updateElide();

    int                        m_columns;
    QVector<PopupPickerEntry>  m_entries;
    QVariant                   m_value;
    QLabel*                    m_arrow = nullptr;
    QLabel*                    m_label = nullptr;
    QString                    m_fullText;
    QElapsedTimer              m_lastCloseTimer;
};

// ── ElidedLabel ──────────────────────────────────────────────
// QLabel that shows its text elided with "…" to fit the available width,
// and never forces the row wider (Ignored h-policy) — so a long name
// can't push neighbours (e.g. a gutter icon) past the column edge, the
// way a plain QLabel's full-text sizeHint would.

class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget* parent = nullptr);
    void setFullText(const QString& t);
    QString fullText() const { return m_full; }

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void updateElide();
    QString m_full;
};

// ── Small helpers ────────────────────────────────────────────

QLabel*      makeParamLabel(const QString& text);
QLabel*      makeSectionTitle(const QString& text);
QFrame*      makeSeparatorLine();
QPushButton* makeIconButton(const QString& iconRes);

// Control name + its control, stacked with the standard tight gap
// (Ui::kGapLabelToCtrl). Add the result to a body layout whose spacing is
// Ui::kGapRows — that is THE way every labelled control is laid out, so the
// label→control and control→next-label gaps stay identical app-wide.
QWidget*     makeLabeledGroup(const QString& label, QWidget* control);
QWidget*     makeLabeledGroup(const QString& label, QLayout* control);

// Make a scroll area's vertical scrollbar auto-hide: it fades in only while
// the user is actually scrolling (and only exists when content overflows),
// then fades back out shortly after.
class QAbstractScrollArea;
void installAutoHideScrollbar(QAbstractScrollArea* area);

// Like installAutoHideScrollbar, but the scrollbar floats over the content
// (reserves no width), so content/dividers reach the panel's right edge.
void installOverlayScrollbar(QAbstractScrollArea* area);
