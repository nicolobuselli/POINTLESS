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
#include <QGuiApplication>
#include <QPainterPath>
#include <QStyle>
#include <cmath>

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

void DragSpinBox::setCompact()
{
    m_compact = true;
    setObjectName("dragSpinBoxCompact");   // QSS: smaller min/max-height
    setFixedHeight(22);
    layout()->setContentsMargins(12, 0, 12, 0);
    m_valueLbl->setStyleSheet("color:#E3E3E3; font-size:9pt; background:transparent;");
    // Re-polish so the new objectName picks up its QSS rule.
    style()->unpolish(this);
    style()->polish(this);
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
        m_lineEdit->setStyleSheet(QString(
            "background:transparent; color:#E3E3E3; font-size:%1pt; border:none; padding:0;")
            .arg(m_compact ? 9 : 10));
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

bool ColorPickerDialog::eventFilter(QObject* obj, QEvent* ev)
{
    if (!m_pickMode) return QDialog::eventFilter(obj, ev);
    if (ev->type() == QEvent::MouseButtonPress) {
        auto* mev = static_cast<QMouseEvent*>(ev);
        const QPoint gp = mev->globalPosition().toPoint();
        QScreen* s = QGuiApplication::screenAt(gp);
        if (s) {
            QPixmap pm = s->grabWindow(0, gp.x(), gp.y(), 1, 1);
            if (!pm.isNull()) {
                QColor c = pm.toImage().pixelColor(0, 0);
                c.getHsvF(&m_h, &m_s, &m_v);
                if (m_h < 0) m_h = 0.f;
                // keep existing m_a (do not sample alpha)
                updateAllFromHSV();
                if (onColorChanged) onColorChanged(selectedColor(), m_a);
            }
        }
        m_pickMode = false;
        qApp->restoreOverrideCursor();
        qApp->removeEventFilter(this);
        return true;
    }
    return QDialog::eventFilter(obj, ev);
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
//  LevelsWidget
// ============================================================

namespace {
    static constexpr int kLvHistH   = 92;   // rounded histogram box height
    static constexpr int kLvStripH  = 16;   // handle strip below the box
    static constexpr int kLvTrackH  = kLvHistH + kLvStripH;   // interactive zone
    static constexpr int kLvRadius  = 10;   // histogram box corner radius
    static constexpr int kLvHandleW = 13;   // handle base width (odd)
    static constexpr int kLvMarginX = 8;    // side padding (handles stay inside)
    static constexpr int kLvBoxH    = 22;   // compact input box height
    static constexpr int kLvGap     = 6;
    static constexpr int kLvTotalH  = kLvTrackH + kLvGap + kLvBoxH;
} // namespace

LevelsWidget::LevelsWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(kLvTotalH);
    setCursor(Qt::SizeHorCursor);

    // Layout only occupies the bottom strip; top is custom-painted.
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, kLvTrackH + kLvGap, 0, 0);
    vl->setSpacing(0);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);
    m_boxBlack = new DragSpinBox("", 0,   255,   0, "", this);
    m_boxMid   = new DragSpinBox("", 10,  500, 100, "", this);
    m_boxWhite = new DragSpinBox("", 0,   255, 255, "", this);
    for (DragSpinBox* b : { m_boxBlack, m_boxMid, m_boxWhite })
        b->setCompact();
    row->addWidget(m_boxBlack, 1);
    row->addWidget(m_boxMid,   1);
    row->addWidget(m_boxWhite, 1);
    vl->addLayout(row);

    auto boxChanged = [this](int) {
        if (m_updating) return;
        m_black = qBound(0,  m_boxBlack->value(), 254);
        m_white = qBound(m_black + 1, m_boxWhite->value(), 255);
        m_mid   = qBound(10, m_boxMid->value(), 500);
        syncBoxes();
        update();
        if (onChanged) onChanged();
    };
    m_boxBlack->onValueChanged = boxChanged;
    m_boxMid->onValueChanged   = boxChanged;
    m_boxWhite->onValueChanged = boxChanged;
}

float LevelsWidget::valToX(int v) const
{
    const float usable = float(width()) - 2.0f * kLvMarginX;
    return kLvMarginX + v * usable / 255.0f;
}

int LevelsWidget::xToVal(float x) const
{
    const float usable = float(width()) - 2.0f * kLvMarginX;
    if (usable <= 0.0f) return 0;
    return qBound(0, int(std::round((x - kLvMarginX) * 255.0f / usable)), 255);
}

float LevelsWidget::midFracFrom(int midV) const
{
    // mid=100 → 0.5 (centre), mid→10 → 0.0 (left/dark), mid=500 → 1.0 (right/bright)
    const float t = std::log(midV / 100.0f) / std::log(5.0f);
    return qBound(0.0f, 0.5f + t * 0.5f, 1.0f);
}

float LevelsWidget::midPixX() const
{
    const float bx   = valToX(m_black);
    const float wx   = valToX(m_white);
    const float span = wx - bx;
    return (span > 0.5f) ? bx + midFracFrom(m_mid) * span : (bx + wx) * 0.5f;
}

int LevelsWidget::midValFromX(float x) const
{
    const float bx   = valToX(m_black);
    const float wx   = valToX(m_white);
    const float span = wx - bx;
    if (span < 1.0f) return 100;
    const float frac = qBound(0.0f, (x - bx) / span, 1.0f);
    const float t    = (frac - 0.5f) * 2.0f;
    return qBound(10, int(std::round(100.0f * std::pow(5.0f, t))), 500);
}

LevelsWidget::Handle LevelsWidget::hitTest(QPoint pos) const
{
    if (pos.y() < kLvHistH - 4 || pos.y() > kLvTrackH + 2) return Handle::None;
    const float bx = valToX(m_black);
    const float wx = valToX(m_white);
    const float mx = midPixX();
    const float r  = float(kLvHandleW) * 0.7f;
    if (std::fabs(float(pos.x()) - mx) <= r) return Handle::Mid;
    if (std::fabs(float(pos.x()) - bx) <= r) return Handle::Black;
    if (std::fabs(float(pos.x()) - wx) <= r) return Handle::White;
    return Handle::None;
}

void LevelsWidget::drawHandle(QPainter& p, float x, Handle h, bool active)
{
    // Photoshop-style "house" pentagon pointing up at the histogram box.
    const float tipY      = float(kLvHistH) + 2.0f;
    const float shoulderY = tipY + 5.0f;
    const float baseY     = float(kLvTrackH) - 1.0f;
    const float hw        = float(kLvHandleW) * 0.5f;

    const QPolygonF pent = QPolygonF()
        << QPointF(x - hw, baseY)
        << QPointF(x - hw, shoulderY)
        << QPointF(x, tipY)
        << QPointF(x + hw, shoulderY)
        << QPointF(x + hw, baseY);

    QColor fill, stroke;
    switch (h) {
        case Handle::Black:
            fill   = QColor(0x0A, 0x0A, 0x0A);
            stroke = active ? QColor("#D0D0D0") : QColor("#5D5D5D");
            break;
        case Handle::Mid:
            fill   = QColor(0x82, 0x82, 0x82);
            stroke = active ? QColor("#E0E0E0") : QColor("#A0A0A0");
            break;
        case Handle::White:
            fill   = QColor(0xFF, 0xFF, 0xFF);
            stroke = active ? QColor("#FFFFFF") : QColor("#909090");
            break;
        default: return;
    }

    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(stroke, 1.0f));
    p.setBrush(fill);
    p.drawPolygon(pent);
}

void LevelsWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int w = width();

    // Rounded histogram box
    const QRectF box(0.5, 0.5, w - 1.0, kLvHistH - 1.0);
    QPainterPath boxPath;
    boxPath.addRoundedRect(box, kLvRadius, kLvRadius);
    p.setPen(QPen(QColor("#3B3B3B"), 1));
    p.setBrush(QColor(0x27, 0x27, 0x27));
    p.drawPath(boxPath);

    // Histogram (log-scaled, light fill rising from the bottom of the box)
    if (m_hasHistogram) {
        float maxLog = 0.0f;
        for (int i = 0; i < 256; ++i)
            maxLog = std::max(maxLog, std::log1p(float(m_histogram[i])));

        if (maxLog > 0.0f) {
            const float usable  = float(w) - 2.0f * kLvMarginX;
            const float bottomY = float(kLvHistH) - 1.0f;
            const float barH    = float(kLvHistH) - 10.0f;

            QPolygonF poly;
            poly.reserve(258);
            poly << QPointF(float(kLvMarginX), bottomY);
            for (int i = 0; i < 256; ++i) {
                const float x = kLvMarginX + i * usable / 255.0f;
                const float t = std::log1p(float(m_histogram[i])) / maxLog;
                poly << QPointF(x, bottomY - t * barH);
            }
            poly << QPointF(float(kLvMarginX) + usable, bottomY);

            p.save();
            p.setClipPath(boxPath);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xD9, 0xD9, 0xD9));
            p.drawPolygon(poly);
            p.restore();
        }
    }

    // Handles below the box (black + white behind, mid on top)
    drawHandle(p, valToX(m_black), Handle::Black, m_dragging == Handle::Black);
    drawHandle(p, valToX(m_white), Handle::White, m_dragging == Handle::White);
    drawHandle(p, midPixX(),       Handle::Mid,   m_dragging == Handle::Mid);
}

void LevelsWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    if (e->pos().y() > kLvTrackH) return;
    m_dragging = hitTest(e->pos());
    if (m_dragging != Handle::None) {
        m_dragStart      = e->pos();
        m_dragStartBlack = m_black;
        m_dragStartMid   = m_mid;
        m_dragStartWhite = m_white;
        e->accept();
    }
}

void LevelsWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging == Handle::None) return;
    const float dx = float(e->pos().x() - m_dragStart.x());

    switch (m_dragging) {
        case Handle::Black:
            m_black = qBound(0, xToVal(valToX(m_dragStartBlack) + dx), m_white - 1);
            break;
        case Handle::White:
            m_white = qBound(m_black + 1, xToVal(valToX(m_dragStartWhite) + dx), 255);
            break;
        case Handle::Mid: {
            const float origMX = valToX(m_black)
                               + midFracFrom(m_dragStartMid)
                               * (valToX(m_white) - valToX(m_black));
            const float newMX  = qBound(valToX(m_black), origMX + dx, valToX(m_white));
            m_mid = midValFromX(newMX);
            break;
        }
        default: break;
    }

    update();
    syncBoxes();
    if (onChanged) onChanged();
}

void LevelsWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_dragging != Handle::None) {
        m_dragging = Handle::None;
        update();
    }
}

void LevelsWidget::syncBoxes()
{
    m_updating = true;
    m_boxBlack->setValue(m_black);
    m_boxMid->setValue(m_mid);
    m_boxWhite->setValue(m_white);
    m_updating = false;
}

void LevelsWidget::setHistogram(const std::array<int,256>& h)
{
    m_histogram = h;
    m_hasHistogram = false;
    for (int v : h) if (v > 0) { m_hasHistogram = true; break; }
    update();
}

void LevelsWidget::setValues(int black, int mid, int white)
{
    m_black = qBound(0,  black, 254);
    m_white = qBound(m_black + 1, white, 255);
    m_mid   = qBound(10, mid, 500);
    syncBoxes();
    update();
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

    // Selectable / hand-editable hex value over the painted area.
    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setFrame(false);
    m_hexEdit->setMaxLength(7);   // allow leading '#'
    m_hexEdit->setCursor(Qt::IBeamCursor);
    m_hexEdit->setStyleSheet(
        "QLineEdit { background: transparent; border: none; color: #E3E3E3;"
        " padding: 0; margin: 0; min-height: 0; font-size: 10pt; }");
    syncHexText();

    connect(m_hexEdit, &QLineEdit::returnPressed, m_hexEdit, [this]() {
        m_hexEdit->clearFocus();   // triggers editingFinished
    });
    connect(m_hexEdit, &QLineEdit::editingFinished, m_hexEdit, [this]() {
        const QString txt = m_hexEdit->text().trimmed().remove('#');
        if (txt.length() == 6) {
            const QColor parsed(QString("#") + txt);
            if (parsed.isValid() && parsed != m_color) {
                m_color = parsed;
                update();
                if (onColorEdited) onColorEdited(m_color);
            }
        }
        syncHexText();   // normalise display (or revert invalid input)
    });
}

void FillSwatch::setColor(QColor c)   { m_color = c; syncHexText(); update(); }
void FillSwatch::setOpacity(float op) { m_opacity = qBound(0.f, op, 1.f); update(); }

void FillSwatch::syncHexText()
{
    m_hexEdit->blockSignals(true);
    m_hexEdit->setText(m_color.name().mid(1).toUpper());
    m_hexEdit->blockSignals(false);
}

void FillSwatch::placeHexEdit()
{
    const int padH   = 7;
    const int sqSize = 20;
    const int textX  = padH + sqSize + 6;
    const int divX   = m_showOpacity ? dividerX() : width() - padH;
    m_hexEdit->setGeometry(textX, 0, qMax(10, divX - textX - 4), height());
}

void FillSwatch::resizeEvent(QResizeEvent*)
{
    placeHexEdit();
}

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

    const int divX = m_showOpacity ? dividerX() : width() - padH;

    // Hex value is shown by the embedded QLineEdit (selectable/editable).

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

void FillSwatch::mouseReleaseEvent(QMouseEvent* e)
{
    // Opacity is edited only from the color picker; the chip just opens it.
    if (e->button() == Qt::LeftButton && onClicked) onClicked();
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
    explicit HueBarWidget(QWidget* p = nullptr) : QWidget(p) { setFixedHeight(16); setCursor(Qt::SizeHorCursor); }
    void setHue(float h) { m_hue = h; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing, true);
        const int H = height();
        QLinearGradient g(0, 0, width(), 0);
        for (int i = 0; i <= 12; i++) { QColor c; c.setHsvF(i / 12.0, 1, 1); g.setColorAt(i / 12.0, c); }
        p.setBrush(g); p.setPen(Qt::NoPen); p.drawRoundedRect(rect(), H / 2.0, H / 2.0);
        const int r = H / 2;
        int x = qBound(r, int(m_hue * width()), width() - r);
        QColor hc; hc.setHsvF(qBound(0.0, (double)m_hue, 1.0), 1, 1);
        QPointF c(x, H / 2.0);
        p.setPen(QPen(QColor(0, 0, 0, 90), 1)); p.setBrush(hc);
        p.drawEllipse(c, r - 1.0, r - 1.0);
        p.setPen(QPen(Qt::white, 2)); p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r - 2.0, r - 2.0);
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
    explicit OpacityBarWidget(QWidget* p = nullptr) : QWidget(p) { setFixedHeight(16); setCursor(Qt::SizeHorCursor); }
    void setColor(QColor c) { m_color = c; update(); }
    void setAlpha(float a)  { m_alpha = a; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing, true);
        const int H = height();
        QPainterPath clip; clip.addRoundedRect(rect(), H / 2.0, H / 2.0);
        p.setClipPath(clip);
        int cs = 4;
        for (int x = 0; x < width(); x += cs)
            for (int y = 0; y < height(); y += cs)
                p.fillRect(x, y, cs, cs, ((x/cs + y/cs) % 2) ? QColor(180,180,180) : QColor(80,80,80));
        QColor op = m_color; op.setAlphaF(1.0f);
        QLinearGradient g(0, 0, width(), 0);
        g.setColorAt(0, QColor(op.red(), op.green(), op.blue(), 0));
        g.setColorAt(1, op);
        p.setBrush(g); p.setPen(Qt::NoPen); p.drawRoundedRect(rect(), H / 2.0, H / 2.0);
        p.setClipping(false);
        const int r = H / 2;
        int x = qBound(r, int(m_alpha * width()), width() - r);
        QColor knob = m_color; knob.setAlphaF(qBound(0.f, m_alpha, 1.f));
        QPointF c(x, H / 2.0);
        p.setPen(QPen(QColor(0, 0, 0, 90), 1)); p.setBrush(knob);
        p.drawEllipse(c, r - 1.0, r - 1.0);
        p.setPen(QPen(Qt::white, 2)); p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r - 2.0, r - 2.0);
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
    setFixedSize(300, showOpacity ? 350 : 300);

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
    const QString kInputQss =
        "QLineEdit{background:#272727;border:1px solid #5D5D5D;border-radius:6px;"
        "color:#E3E3E3;font-size:9pt;padding:0 8px;min-height:34px;}"
        "QLineEdit:focus{border-color:#828282;}";

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // Saturation / value field
    m_field = new ColorFieldWidget;
    m_field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addWidget(m_field, 1);

    // Eyedropper (left) + hue / opacity bars (right)
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(12);

        auto* eyeBtn = new QPushButton;
        eyeBtn->setObjectName("eyeDropBtn");
        eyeBtn->setCursor(Qt::PointingHandCursor);
        eyeBtn->setFixedSize(30, 30);
        eyeBtn->setIconSize(QSize(18, 18));
        eyeBtn->setIcon(QIcon(":/icons/color_picker.svg"));
        eyeBtn->setToolTip("Pick color from screen");
        eyeBtn->setStyleSheet(
            "QPushButton#eyeDropBtn{background:transparent;border:none;border-radius:6px;}"
            "QPushButton#eyeDropBtn:hover{background:#2E2E2E;}");
        row->addWidget(eyeBtn, 0, Qt::AlignVCenter);

        auto* bars = new QVBoxLayout;
        bars->setSpacing(10);
        m_hueBar = new HueBarWidget;
        bars->addWidget(m_hueBar);
        if (showOpacity) {
            m_opBar = new OpacityBarWidget;
            bars->addWidget(m_opBar);
        }
        row->addLayout(bars, 1);
        root->addLayout(row);

        connect(eyeBtn, &QPushButton::clicked, this, [this]() {
            m_pickMode = true;
            qApp->setOverrideCursor(Qt::CrossCursor);
            qApp->installEventFilter(this);
        });
    }

    // Hex value + opacity %
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);

        m_hexInput = new QLineEdit;
        m_hexInput->setStyleSheet(kInputQss);
        row->addWidget(m_hexInput, 1);

        if (showOpacity) {
            m_opacityInput = new QLineEdit;
            m_opacityInput->setAlignment(Qt::AlignCenter);
            m_opacityInput->setFixedWidth(52);
            m_opacityInput->setStyleSheet(kInputQss);
            row->addWidget(m_opacityInput);

            auto* pct = new QLabel("%");
            pct->setStyleSheet("color:#B2B2B2;font-size:9pt;background:transparent;");
            row->addWidget(pct);
        }
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
    if (m_opacityInput) {
        connect(m_opacityInput, &QLineEdit::editingFinished, m_opacityInput, [this]() {
            bool ok; int v = m_opacityInput->text().remove('%').trimmed().toInt(&ok);
            if (ok) {
                m_a = qBound(0.f, v / 100.0f, 1.f);
                updateAllFromHSV();
                if (onColorChanged) onColorChanged(selectedColor(), m_a);
            } else {
                m_opacityInput->setText(QString::number(qRound(m_a * 100.0f)));
            }
        });
    }
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
    if (m_hexInput && !m_hexInput->hasFocus())
        m_hexInput->setText(c.name().mid(1).toUpper());
    if (m_opacityInput && !m_opacityInput->hasFocus())
        m_opacityInput->setText(QString::number(qRound(m_a * 100.0f)));
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
