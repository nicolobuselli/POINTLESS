#include "Widgets.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScreen>
#include <QIcon>
#include <QEvent>

// ============================================================
//  NoWheel*
// ============================================================

void NoWheelSlider::wheelEvent(QWheelEvent* e)   { e->ignore(); }
void NoWheelComboBox::wheelEvent(QWheelEvent* e) { e->ignore(); }

// ============================================================
//  DragSpinBox
// ============================================================

DragSpinBox::DragSpinBox(const QString& iconRes, int minVal, int maxVal, int defVal,
                         const QString& suffix, QWidget* parent)
    : QFrame(parent), m_min(minVal), m_max(maxVal), m_value(defVal), m_suffix(suffix)
{
    setObjectName("dragSpinBox");
    setFrameShape(QFrame::NoFrame);
    setCursor(Qt::SizeHorCursor);
    setFixedHeight(38);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 1, 8, 1);
    lay->setSpacing(6);

    const bool hasIcon = !iconRes.isEmpty();
    if (hasIcon) {
        m_iconLbl = new QLabel(this);
        m_iconLbl->setPixmap(QIcon(iconRes).pixmap(16, 16));
        m_iconLbl->setFixedSize(16, 16);
        m_iconLbl->setStyleSheet("background:transparent;");
        m_iconLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(m_iconLbl);
    }

    m_valueLbl = new QLabel(this);
    m_valueLbl->setAlignment(Qt::AlignVCenter | (hasIcon ? Qt::AlignRight : Qt::AlignHCenter));
    m_valueLbl->setStyleSheet("color:#E3E3E3; font-size:10pt; background:transparent;");
    m_valueLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    lay->addWidget(m_valueLbl, 1);

    updateDisplay();
}

void DragSpinBox::setValue(int v)
{
    m_value = qBound(m_min, v, m_max);
    updateDisplay();
}

void DragSpinBox::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStart = e->globalPosition().toPoint();
        m_dragStartVal = m_value;
    }
}

void DragSpinBox::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging) return;
    int dx = e->globalPosition().toPoint().x() - m_dragStart.x();
    int nv = qBound(m_min, m_dragStartVal + dx / 2, m_max);
    if (nv != m_value) {
        m_value = nv;
        updateDisplay();
        if (onValueChanged) onValueChanged(m_value);
    }
}

void DragSpinBox::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) m_dragging = false;
}

void DragSpinBox::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    m_editingValue = true;
    if (!m_lineEdit) {
        m_lineEdit = new QLineEdit(this);
        m_lineEdit->setFrame(false);
        m_lineEdit->setAlignment(m_valueLbl->alignment());
        m_lineEdit->setStyleSheet(
            "background:transparent; color:#E3E3E3; font-size:10pt; border:none; padding:0;");
        connect(m_lineEdit, &QLineEdit::editingFinished, m_lineEdit, [this]() {
            bool ok; int v = m_lineEdit->text().toInt(&ok);
            if (ok) { setValue(v); if (onValueChanged) onValueChanged(m_value); }
            m_editingValue = false;
            m_lineEdit->hide();
            updateDisplay();
        });
    }
    // Keep layout stable while editing: preserve value-label slot and edit only in that area.
    m_valueLbl->setText(QString());
    m_lineEdit->setGeometry(m_valueLbl->geometry().adjusted(0, 0, 0, 0));
    m_lineEdit->setText(QString::number(m_value));
    m_lineEdit->show(); m_lineEdit->setFocus(); m_lineEdit->selectAll();
}

void DragSpinBox::updateDisplay()
{
    if (m_valueLbl && !m_editingValue) m_valueLbl->setText(QString::number(m_value) + m_suffix);
}

// ============================================================
//  SliderRow
// ============================================================

SliderRow::SliderRow(const QString& label, int minVal, int maxVal, int defVal,
                     QWidget* parent)
    : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(4);

    if (!label.isEmpty())
        vl->addWidget(makeParamLabel(label));

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    m_slider = new NoWheelSlider(Qt::Horizontal);
    m_slider->setRange(minVal, maxVal);
    m_slider->setValue(defVal);

    m_box = new DragSpinBox(QString(), minVal, maxVal, defVal);
    m_box->setFixedSize(48, 38);

    row->addWidget(m_slider, 1);
    row->addWidget(m_box);
    vl->addLayout(row);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        m_updating = true;
        m_box->setValue(v);
        m_updating = false;
        if (onValueChanged) onValueChanged(v);
    });
    m_box->onValueChanged = [this](int v) {
        if (m_updating) return;
        m_updating = true;
        m_slider->blockSignals(true);
        m_slider->setValue(v);
        m_slider->blockSignals(false);
        m_updating = false;
        if (onValueChanged) onValueChanged(v);
    };
}

int SliderRow::value() const { return m_slider->value(); }

void SliderRow::setValue(int v)
{
    m_updating = true;
    m_slider->blockSignals(true);
    m_slider->setValue(v);
    m_slider->blockSignals(false);
    m_box->setValue(m_slider->value());
    m_updating = false;
}

// ============================================================
//  ChevronButton
// ============================================================

ChevronButton::ChevronButton(Direction dir, QWidget* parent)
    : QPushButton(parent), m_dir(dir)
{
    setObjectName("iconBtn");
    setFixedSize(24, 24);
    setCursor(Qt::PointingHandCursor);
}

void ChevronButton::setDirection(Direction dir)
{
    m_dir = dir;
    update();
}

void ChevronButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (underMouse()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#3B3B3B"));
        p.drawRoundedRect(rect(), 4, 4);
    }

    QPen pen(QColor("#B2B2B2"), 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const QPointF c(width() / 2.0, height() / 2.0);
    const qreal s = 3.5;
    QPolygonF poly;
    switch (m_dir) {
        case Up:    poly << QPointF(c.x() - s, c.y() + s/2) << QPointF(c.x(), c.y() - s/2) << QPointF(c.x() + s, c.y() + s/2); break;
        case Down:  poly << QPointF(c.x() - s, c.y() - s/2) << QPointF(c.x(), c.y() + s/2) << QPointF(c.x() + s, c.y() - s/2); break;
        case Left:  poly << QPointF(c.x() + s/2, c.y() - s) << QPointF(c.x() - s/2, c.y()) << QPointF(c.x() + s/2, c.y() + s); break;
        case Right: poly << QPointF(c.x() - s/2, c.y() - s) << QPointF(c.x() + s/2, c.y()) << QPointF(c.x() - s/2, c.y() + s); break;
    }
    p.drawPolyline(poly);
}

// ============================================================
//  CollapsibleSection
// ============================================================

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* content,
                                       QWidget* parent)
    : QWidget(parent), m_content(content)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    m_header = new QWidget;
    m_header->setCursor(Qt::PointingHandCursor);
    auto* hl = new QHBoxLayout(m_header);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);

    hl->addWidget(makeSectionTitle(title), 1);

    m_extras = new QWidget;
    auto* el = new QHBoxLayout(m_extras);
    el->setContentsMargins(0, 0, 0, 0);
    el->setSpacing(6);
    hl->addWidget(m_extras);

    m_chevron = new ChevronButton(ChevronButton::Up);
    hl->addWidget(m_chevron);

    vl->addWidget(m_header);
    vl->addWidget(m_content);

    m_header->installEventFilter(this);
    connect(m_chevron, &QPushButton::clicked, this, [this]() {
        setExpanded(!m_expanded);
    });
}

void CollapsibleSection::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_content->setVisible(expanded);
    m_chevron->setDirection(expanded ? ChevronButton::Up : ChevronButton::Down);
}

bool CollapsibleSection::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_header && ev->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(ev);
        // Don't toggle when clicking buttons hosted in the extras slot.
        if (me->button() == Qt::LeftButton && !m_extras->geometry().contains(me->pos())) {
            setExpanded(!m_expanded);
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

// ============================================================
//  FillSwatch
// ============================================================

FillSwatch::FillSwatch(QColor color, float opacity, bool showOpacity, QWidget* parent)
    : QWidget(parent), m_color(color), m_opacity(opacity), m_showOpacity(showOpacity)
{
    setFixedHeight(34);
    setCursor(Qt::PointingHandCursor);
}

void FillSwatch::setColor(QColor c)   { m_color = c; update(); }
void FillSwatch::setOpacity(float op) { m_opacity = qBound(0.f, op, 1.f); update(); }

int FillSwatch::dividerX() const { return width() - 55; }

void FillSwatch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.setPen(QPen(QColor("#5D5D5D"), 1));
    p.setBrush(QColor("#3B3B3B"));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);

    const int h      = height();
    const int padH   = 7;
    const int sqSize = 20;
    const int sqY    = (h - sqSize) / 2;

    // Checkerboard behind the chip when semi-transparent
    if (m_showOpacity && m_opacity < 0.999f) {
        p.setPen(Qt::NoPen);
        const int cs = 5;
        for (int xx = 0; xx < sqSize; xx += cs)
            for (int yy = 0; yy < sqSize; yy += cs)
                p.fillRect(padH + xx, sqY + yy, qMin(cs, sqSize - xx), qMin(cs, sqSize - yy),
                           ((xx / cs + yy / cs) % 2) ? QColor(150,150,150) : QColor(90,90,90));
    }

    QColor chip = m_color;
    if (m_showOpacity) chip.setAlphaF(m_opacity);
    p.setPen(Qt::NoPen);
    p.setBrush(chip);
    p.drawRoundedRect(QRectF(padH, sqY, sqSize, sqSize), 3, 3);

    QFont f;
    f.setFamilies({"Funnel Display", "Segoe UI", "Arial"});
    f.setPixelSize(13);
    f.setWeight(QFont::Medium);
    p.setFont(f);

    const int divX   = m_showOpacity ? dividerX() : width() - padH;
    const int textX  = padH + sqSize + 6;

    p.setPen(QColor("#E3E3E3"));
    p.drawText(QRect(textX, 0, divX - textX - 6, h),
               Qt::AlignVCenter | Qt::AlignLeft,
               m_color.name().mid(1).toUpper());

    if (m_showOpacity) {
        int divH   = 28;
        int divTop = (h - divH) / 2;
        p.setPen(QPen(QColor("#272727"), 1));
        p.drawLine(divX, divTop, divX, divTop + divH);

        p.setPen(QColor("#E3E3E3"));
        p.setFont(f);
        p.drawText(QRect(divX + 1, 0, width() - divX - 1, h),
                   Qt::AlignVCenter | Qt::AlignCenter,
                   QString::number(qRound(m_opacity * 100)) + "%");
    }
}

void FillSwatch::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_pressPos    = e->pos();
        m_dragging    = false;
        m_dragStartOp = m_opacity;
    }
}

void FillSwatch::mouseMoveEvent(QMouseEvent* e)
{
    if (!(e->buttons() & Qt::LeftButton) || !m_showOpacity) return;
    int dx = e->pos().x() - m_pressPos.x();
    if (qAbs(dx) > 4) {
        m_dragging = true;
        if (m_pressPos.x() > dividerX()) {
            float nop = qBound(0.f, m_dragStartOp + dx / 200.f, 1.f);
            if (!qFuzzyCompare(nop, m_opacity)) {
                m_opacity = nop;
                update();
                if (onOpacityDragged) onOpacityDragged(m_opacity);
            }
        }
    }
}

void FillSwatch::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && !m_dragging)
        if (onClicked) onClicked();
    m_dragging = false;
}

// ============================================================
//  Color picker internals
// ============================================================

class ColorFieldWidget : public QWidget {
public:
    std::function<void(float s, float v)> onChanged;
    explicit ColorFieldWidget(QWidget* p = nullptr) : QWidget(p) {
        setMinimumSize(200, 150); setCursor(Qt::CrossCursor);
    }
    void setHue(float h)         { m_hue = h; update(); }
    void setSV(float s, float v) { m_s = s; m_v = v; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        QColor hc; hc.setHsvF(qBound(0.0, (double)m_hue, 1.0), 1.0, 1.0);
        QLinearGradient hg(0, 0, width(), 0);
        hg.setColorAt(0, Qt::white); hg.setColorAt(1, hc);
        p.fillRect(rect(), hg);
        QLinearGradient vg(0, 0, 0, height());
        vg.setColorAt(0, QColor(0,0,0,0)); vg.setColorAt(1, QColor(0,0,0,255));
        p.fillRect(rect(), vg);
        int cx = int(m_s * width());
        int cy = int((1.0f - m_v) * height());
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(Qt::black, 2)); p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(cx, cy), 7, 7);
        p.setPen(QPen(Qt::white, 2));
        p.drawEllipse(QPointF(cx, cy), 6, 6);
    }
    void mousePressEvent(QMouseEvent* e) override { if (e->button() == Qt::LeftButton) pick(e->pos()); }
    void mouseMoveEvent(QMouseEvent* e)  override { if (e->buttons() & Qt::LeftButton)  pick(e->pos()); }
private:
    void pick(QPoint pos) {
        m_s = qBound(0.f, float(pos.x()) / width(), 1.f);
        m_v = qBound(0.f, 1.f - float(pos.y()) / height(), 1.f);
        update(); if (onChanged) onChanged(m_s, m_v);
    }
    float m_hue = 0.f, m_s = 1.f, m_v = 1.f;
};

class HueBarWidget : public QWidget {
public:
    std::function<void(float h)> onChanged;
    explicit HueBarWidget(QWidget* p = nullptr) : QWidget(p) { setFixedHeight(14); setCursor(Qt::SizeHorCursor); }
    void setHue(float h) { m_hue = h; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing, false);
        QLinearGradient g(0, 0, width(), 0);
        for (int i = 0; i <= 12; i++) { QColor c; c.setHsvF(i / 12.0, 1, 1); g.setColorAt(i / 12.0, c); }
        p.setBrush(g); p.setPen(Qt::NoPen); p.drawRoundedRect(rect(), 3, 3);
        int x = qBound(3, int(m_hue * width()), width() - 4);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(Qt::white, 2)); p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(x - 4, 1, 8, height() - 2, 2, 2);
    }
    void mousePressEvent(QMouseEvent* e) override { if (e->button() == Qt::LeftButton) pick(e->pos()); }
    void mouseMoveEvent(QMouseEvent* e)  override { if (e->buttons() & Qt::LeftButton)  pick(e->pos()); }
private:
    void pick(QPoint pos) { m_hue = qBound(0.f, float(pos.x()) / width(), 1.f); update(); if (onChanged) onChanged(m_hue); }
    float m_hue = 0.f;
};

class OpacityBarWidget : public QWidget {
public:
    std::function<void(float a)> onChanged;
    explicit OpacityBarWidget(QWidget* p = nullptr) : QWidget(p) { setFixedHeight(14); setCursor(Qt::SizeHorCursor); }
    void setColor(QColor c) { m_color = c; update(); }
    void setAlpha(float a)  { m_alpha = a; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing, false);
        int cs = 4;
        for (int x = 0; x < width(); x += cs)
            for (int y = 0; y < height(); y += cs)
                p.fillRect(x, y, cs, cs, ((x/cs + y/cs) % 2) ? QColor(180,180,180) : QColor(80,80,80));
        QColor op = m_color; op.setAlphaF(1.0f);
        QLinearGradient g(0, 0, width(), 0);
        g.setColorAt(0, QColor(op.red(), op.green(), op.blue(), 0));
        g.setColorAt(1, op);
        p.setBrush(g); p.setPen(Qt::NoPen); p.drawRoundedRect(rect(), 3, 3);
        int x = qBound(3, int(m_alpha * width()), width() - 4);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(Qt::white, 2)); p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(x - 4, 1, 8, height() - 2, 2, 2);
    }
    void mousePressEvent(QMouseEvent* e) override { if (e->button() == Qt::LeftButton) pick(e->pos()); }
    void mouseMoveEvent(QMouseEvent* e)  override { if (e->buttons() & Qt::LeftButton)  pick(e->pos()); }
private:
    void pick(QPoint pos) { m_alpha = qBound(0.f, float(pos.x()) / width(), 1.f); update(); if (onChanged) onChanged(m_alpha); }
    QColor m_color = Qt::red; float m_alpha = 1.f;
};

// ============================================================
//  ColorPickerDialog
// ============================================================

ColorPickerDialog::ColorPickerDialog(QColor initial, float initialOpacity,
                                     bool showOpacity, QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Popup)
    , m_a(initialOpacity)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(false);
    setFixedSize(300, showOpacity ? 315 : 275);

    initial.getHsvF(&m_h, &m_s, &m_v);
    if (m_h < 0) m_h = 0.f;

    buildUI(showOpacity);
    updateAllFromHSV();
}

QColor ColorPickerDialog::selectedColor() const
{
    QColor c; c.setHsvF(qBound(0.0, (double)m_h, 1.0),
                        qBound(0.0, (double)m_s, 1.0),
                        qBound(0.0, (double)m_v, 1.0));
    return c;
}

void ColorPickerDialog::moveNextTo(QWidget* anchor)
{
    if (!anchor) return;
    QPoint gp = anchor->mapToGlobal(QPoint(0, 0));
    QRect sg  = anchor->screen()->availableGeometry();
    int dlgX  = qMax(sg.left(), gp.x() - width() - 8);
    int dlgY  = qBound(sg.top(), gp.y(), sg.bottom() - height());
    move(dlgX, dlgY);
}

void ColorPickerDialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#5D5D5D"), 1));
    p.setBrush(QColor("#1E1E1E"));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);
}

void ColorPickerDialog::mousePressEvent(QMouseEvent* e)
{
    QDialog::mousePressEvent(e);
}

void ColorPickerDialog::mouseMoveEvent(QMouseEvent* e)
{
    QDialog::mouseMoveEvent(e);
}

void ColorPickerDialog::mouseReleaseEvent(QMouseEvent* e)
{
    QDialog::mouseReleaseEvent(e);
}

void ColorPickerDialog::buildUI(bool showOpacity)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(8);

    m_field = new ColorFieldWidget;
    m_field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addWidget(m_field, 1);

    {
        auto* l = new QLabel("Hue");
        l->setStyleSheet("color:#B2B2B2;font-size:8pt;background:transparent;");
        root->addWidget(l);
    }
    m_hueBar = new HueBarWidget;
    root->addWidget(m_hueBar);

    if (showOpacity) {
        auto* l = new QLabel("Opacity");
        l->setStyleSheet("color:#B2B2B2;font-size:8pt;background:transparent;");
        root->addWidget(l);
        m_opBar = new OpacityBarWidget;
        root->addWidget(m_opBar);
    }

    {
        auto* row = new QHBoxLayout; row->setSpacing(8);
        m_preview = new QLabel;
        m_preview->setFixedSize(36, 36);
        m_hexInput = new QLineEdit;
        m_hexInput->setStyleSheet(
            "QLineEdit{background:#272727;border:1px solid #5D5D5D;border-radius:6px;"
            "color:#E3E3E3;font-size:9pt;padding:0 8px;min-height:34px;}"
            "QLineEdit:focus{border-color:#828282;}");
        row->addWidget(m_preview); row->addWidget(m_hexInput, 1);
        root->addLayout(row);
    }


    m_field->onChanged = [this](float s, float v) {
        m_s = s; m_v = v;
        if (m_opBar) m_opBar->setColor(selectedColor());
        updatePreviewAndHex();
        if (onColorChanged) onColorChanged(selectedColor(), m_a);
    };
    m_hueBar->onChanged = [this](float h) {
        m_h = h;
        m_field->setHue(h);
        if (m_opBar) m_opBar->setColor(selectedColor());
        updatePreviewAndHex();
        if (onColorChanged) onColorChanged(selectedColor(), m_a);
    };
    if (m_opBar) {
        m_opBar->onChanged = [this](float a) {
            m_a = a; updatePreviewAndHex();
            if (onColorChanged) onColorChanged(selectedColor(), m_a);
        };
    }
    connect(m_hexInput, &QLineEdit::editingFinished, m_hexInput, [this]() {
        QString txt = m_hexInput->text().trimmed().remove('#');
        if (txt.length() == 6) {
            bool ok; QRgb rgb = txt.toUInt(&ok, 16);
            if (ok) {
                QColor c(rgb); c.getHsvF(&m_h, &m_s, &m_v);
                if (m_h < 0) m_h = 0.f;
                updateAllFromHSV();
                if (onColorChanged) onColorChanged(selectedColor(), m_a);
            }
        }
    });
}

void ColorPickerDialog::updateAllFromHSV()
{
    m_field->setHue(m_h); m_field->setSV(m_s, m_v);
    m_hueBar->setHue(m_h);
    if (m_opBar) { m_opBar->setAlpha(m_a); m_opBar->setColor(selectedColor()); }
    updatePreviewAndHex();
}

void ColorPickerDialog::updatePreviewAndHex()
{
    QColor c = selectedColor();
    m_preview->setStyleSheet(QString(
        "background:%1;border:1px solid #5D5D5D;border-radius:6px;").arg(c.name()));
    if (m_hexInput) m_hexInput->setText(c.name().mid(1).toUpper());
}

// ============================================================
//  Small helpers
// ============================================================

QLabel* makeParamLabel(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName("paramLabel");
    return l;
}

QLabel* makeSectionTitle(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName("sectionTitle");
    return l;
}

QFrame* makeSeparatorLine()
{
    auto* f = new QFrame;
    f->setObjectName("separator");
    f->setFrameShape(QFrame::NoFrame);
    f->setFixedHeight(1);
    return f;
}

QPushButton* makeIconButton(const QString& iconRes)
{
    auto* btn = new QPushButton;
    btn->setObjectName("iconBtn");
    btn->setIcon(QIcon(iconRes));
    btn->setIconSize(QSize(14, 14));
    btn->setFixedSize(24, 24);
    return btn;
}
