#include "ControlPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QDialog>
#include <QFileDialog>
#include <QColor>
#include <QSizePolicy>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QIcon>
#include <QLineEdit>
#include <QScreen>
#include <QFileInfo>
#include <functional>

// ============================================================
//  Helper widgets (anonymous namespace — no Q_OBJECT)
// ============================================================

namespace {

class NoWheelComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;
protected:
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }
};

class NoWheelSlider : public QSlider {
public:
    using QSlider::QSlider;
protected:
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }
};

// ── DragSpinBox ──────────────────────────────────────────────

class DragSpinBox : public QFrame {
public:
    std::function<void(int)> onValueChanged;

    DragSpinBox(const QString& iconRes, int minVal, int maxVal, int defVal,
                const QString& suffix = QString(), QWidget* parent = nullptr)
        : QFrame(parent), m_min(minVal), m_max(maxVal), m_value(defVal), m_suffix(suffix)
    {
        setObjectName("dragSpinBox");
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::SizeHorCursor);
        setFixedHeight(34);

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(8, 0, 8, 0);
        lay->setSpacing(6);

        if (!iconRes.isEmpty()) {
            auto* ico = new QLabel(this);
            ico->setPixmap(QIcon(iconRes).pixmap(16, 16));
            ico->setFixedSize(16, 16);
            ico->setAttribute(Qt::WA_TransparentForMouseEvents);
            lay->addWidget(ico);
        }

        m_valueLbl = new QLabel(this);
        m_valueLbl->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
        m_valueLbl->setStyleSheet("color:#E3E3E3; font-size:10pt; background:transparent;");
        m_valueLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(m_valueLbl, 1);

        updateDisplay();
    }

    int  value() const { return m_value; }
    void setValue(int v) { m_value = qBound(m_min, v, m_max); updateDisplay(); }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragStart = e->globalPosition().toPoint();
            m_dragStartVal = m_value;
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!m_dragging) return;
        int dx = e->globalPosition().toPoint().x() - m_dragStart.x();
        int nv = qBound(m_min, m_dragStartVal + dx / 2, m_max);
        if (nv != m_value) { m_value = nv; updateDisplay(); if (onValueChanged) onValueChanged(m_value); }
    }
    void mouseReleaseEvent(QMouseEvent* e) override { if (e->button() == Qt::LeftButton) m_dragging = false; }
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        if (!m_lineEdit) {
            m_lineEdit = new QLineEdit(this);
            m_lineEdit->setFrame(false);
            m_lineEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_lineEdit->setStyleSheet("background:transparent; color:#E3E3E3; font-size:10pt; border:none; padding:0;");
            connect(m_lineEdit, &QLineEdit::editingFinished, m_lineEdit, [this]() {
                bool ok; int v = m_lineEdit->text().toInt(&ok);
                if (ok) { setValue(v); if (onValueChanged) onValueChanged(m_value); }
                m_lineEdit->hide(); m_valueLbl->show();
            });
        }
        m_valueLbl->hide();
        m_lineEdit->setGeometry(m_valueLbl->geometry());
        m_lineEdit->setText(QString::number(m_value));
        m_lineEdit->show(); m_lineEdit->setFocus(); m_lineEdit->selectAll();
    }

private:
    void updateDisplay() { if (m_valueLbl) m_valueLbl->setText(QString::number(m_value) + m_suffix); }
    QLabel*    m_valueLbl  = nullptr;
    QLineEdit* m_lineEdit  = nullptr;
    int        m_min, m_max, m_value;
    QString    m_suffix;
    bool       m_dragging     = false;
    QPoint     m_dragStart;
    int        m_dragStartVal = 0;
};

// ── Color picker sub-widgets ──────────────────────────────────

class ColorFieldWidget : public QWidget {
public:
    std::function<void(float s, float v)> onChanged;
    explicit ColorFieldWidget(QWidget* p = nullptr) : QWidget(p) {
        setMinimumSize(200, 150); setCursor(Qt::CrossCursor);
    }
    void setHue(float h)         { m_hue = h; update(); }
    void setSV(float s, float v) { m_s = s; m_v = v; update(); }
    float saturation() const     { return m_s; }
    float val()        const     { return m_v; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        QColor hc; hc.setHsvF(qBound(0.0,(double)m_hue,1.0), 1.0, 1.0);
        QLinearGradient hg(0,0,width(),0);
        hg.setColorAt(0, Qt::white); hg.setColorAt(1, hc);
        p.fillRect(rect(), hg);
        QLinearGradient vg(0,0,0,height());
        vg.setColorAt(0, QColor(0,0,0,0)); vg.setColorAt(1, QColor(0,0,0,255));
        p.fillRect(rect(), vg);
        int cx = int(m_s * width());
        int cy = int((1.0f - m_v) * height());
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(Qt::black, 2)); p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(cx,cy), 7, 7);
        p.setPen(QPen(Qt::white, 2));
        p.drawEllipse(QPointF(cx,cy), 6, 6);
    }
    void mousePressEvent(QMouseEvent* e) override { if (e->button()==Qt::LeftButton) pick(e->pos()); }
    void mouseMoveEvent(QMouseEvent* e)  override { if (e->buttons()&Qt::LeftButton)  pick(e->pos()); }
private:
    void pick(QPoint pos) {
        m_s = qBound(0.f, float(pos.x())/width(), 1.f);
        m_v = qBound(0.f, 1.f - float(pos.y())/height(), 1.f);
        update(); if (onChanged) onChanged(m_s, m_v);
    }
    float m_hue=0.f, m_s=1.f, m_v=1.f;
};

class HueBarWidget : public QWidget {
public:
    std::function<void(float h)> onChanged;
    explicit HueBarWidget(QWidget* p=nullptr) : QWidget(p) { setFixedHeight(14); setCursor(Qt::SizeHorCursor); }
    void  setHue(float h) { m_hue=h; update(); }
    float hue()    const  { return m_hue; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing,false);
        QLinearGradient g(0,0,width(),0);
        for (int i=0;i<=12;i++) { QColor c; c.setHsvF(i/12.0,1,1); g.setColorAt(i/12.0,c); }
        p.setBrush(g); p.setPen(Qt::NoPen); p.drawRoundedRect(rect(),3,3);
        int x = qBound(3, int(m_hue*width()), width()-4);
        p.setRenderHint(QPainter::Antialiasing,true);
        p.setPen(QPen(Qt::white,2)); p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(x-4,1,8,height()-2,2,2);
    }
    void mousePressEvent(QMouseEvent* e) override { if (e->button()==Qt::LeftButton) pick(e->pos()); }
    void mouseMoveEvent(QMouseEvent* e)  override { if (e->buttons()&Qt::LeftButton)  pick(e->pos()); }
private:
    void pick(QPoint pos) { m_hue=qBound(0.f,float(pos.x())/width(),1.f); update(); if(onChanged) onChanged(m_hue); }
    float m_hue=0.f;
};

class OpacityBarWidget : public QWidget {
public:
    std::function<void(float a)> onChanged;
    explicit OpacityBarWidget(QWidget* p=nullptr) : QWidget(p) { setFixedHeight(14); setCursor(Qt::SizeHorCursor); }
    void  setColor(QColor c) { m_color=c; update(); }
    void  setAlpha(float a)  { m_alpha=a; update(); }
    float alpha() const      { return m_alpha; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing,false);
        int cs=4;
        for (int x=0;x<width();x+=cs)
            for (int y=0;y<height();y+=cs)
                p.fillRect(x,y,cs,cs, ((x/cs+y/cs)%2) ? QColor(180,180,180) : QColor(80,80,80));
        QColor op=m_color; op.setAlphaF(1.0f);
        QLinearGradient g(0,0,width(),0);
        g.setColorAt(0,QColor(op.red(),op.green(),op.blue(),0));
        g.setColorAt(1,op);
        p.setBrush(g); p.setPen(Qt::NoPen); p.drawRoundedRect(rect(),3,3);
        int x=qBound(3,int(m_alpha*width()),width()-4);
        p.setRenderHint(QPainter::Antialiasing,true);
        p.setPen(QPen(Qt::white,2)); p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(x-4,1,8,height()-2,2,2);
    }
    void mousePressEvent(QMouseEvent* e) override { if (e->button()==Qt::LeftButton) pick(e->pos()); }
    void mouseMoveEvent(QMouseEvent* e)  override { if (e->buttons()&Qt::LeftButton)  pick(e->pos()); }
private:
    void pick(QPoint pos) { m_alpha=qBound(0.f,float(pos.x())/width(),1.f); update(); if(onChanged) onChanged(m_alpha); }
    QColor m_color=Qt::red; float m_alpha=1.f;
};

// ── ColorPickerDialog ─────────────────────────────────────────
// Inherits QDialog (which has Q_OBJECT) — no need for Q_OBJECT here
// since we don't add custom signals/slots.

class ColorPickerDialog : public QDialog {
public:
    std::function<void(QColor, float)> onColorChanged;

    ColorPickerDialog(QColor initial, float initialOpacity, QWidget* parent=nullptr)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
        , m_a(initialOpacity)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setModal(true);
        setFixedSize(300, 370);

        initial.getHsvF(&m_h, &m_s, &m_v);
        if (m_h < 0) m_h = 0.f;

        buildUI();
        updateAllFromHSV();
    }

    QColor selectedColor() const {
        QColor c; c.setHsvF(qBound(0.0,(double)m_h,1.0),
                            qBound(0.0,(double)m_s,1.0),
                            qBound(0.0,(double)m_v,1.0));
        return c;
    }
    float selectedOpacity() const { return m_a; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#5D5D5D"),1));
        p.setBrush(QColor("#1E1E1E"));
        p.drawRoundedRect(rect().adjusted(1,1,-1,-1), 10, 10);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button()==Qt::LeftButton && e->pos().y()<44)
            m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if ((e->buttons()&Qt::LeftButton) && !m_dragOffset.isNull())
            move(e->globalPosition().toPoint() - m_dragOffset);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button()==Qt::LeftButton) m_dragOffset={};
    }

private:
    void buildUI() {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(16,14,16,14);
        root->setSpacing(8);

        // Title
        {
            auto* row = new QHBoxLayout;
            auto* title = new QLabel("Pick Color");
            title->setStyleSheet("color:#EEEEEE;font-size:10pt;font-weight:600;background:transparent;");
            auto* xBtn = new QPushButton("×");
            xBtn->setFixedSize(20,20);
            xBtn->setStyleSheet(
                "QPushButton{background:transparent;border:none;color:#828282;"
                "font-size:16pt;padding:0;}"
                "QPushButton:hover{color:#E3E3E3;}");
            connect(xBtn, &QPushButton::clicked, this, &QDialog::reject);
            row->addWidget(title,1); row->addWidget(xBtn);
            root->addLayout(row);
        }

        // Color field
        m_field = new ColorFieldWidget;
        m_field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        root->addWidget(m_field, 1);

        // Hue
        {
            auto* l = new QLabel("Hue");
            l->setStyleSheet("color:#B2B2B2;font-size:8pt;background:transparent;");
            root->addWidget(l);
        }
        m_hueBar = new HueBarWidget;
        root->addWidget(m_hueBar);

        // Opacity
        {
            auto* l = new QLabel("Opacity");
            l->setStyleSheet("color:#B2B2B2;font-size:8pt;background:transparent;");
            root->addWidget(l);
        }
        m_opBar = new OpacityBarWidget;
        root->addWidget(m_opBar);

        // Preview + hex
        {
            auto* row = new QHBoxLayout; row->setSpacing(8);
            m_preview = new QLabel;
            m_preview->setFixedSize(36,36);
            m_hexInput = new QLineEdit;
            m_hexInput->setStyleSheet(
                "QLineEdit{background:#272727;border:1px solid #5D5D5D;border-radius:6px;"
                "color:#E3E3E3;font-size:9pt;padding:0 8px;min-height:34px;}"
                "QLineEdit:focus{border-color:#828282;}");
            row->addWidget(m_preview); row->addWidget(m_hexInput,1);
            root->addLayout(row);
        }

        // OK / Cancel
        {
            auto* row = new QHBoxLayout; row->setSpacing(8);
            QString bs =
                "QPushButton{background:#3B3B3B;border:1px solid #5D5D5D;border-radius:6px;"
                "color:#E3E3E3;font-size:9pt;min-height:30px;padding:0 16px;}"
                "QPushButton:hover{background:#484848;border-color:#828282;}";
            auto* cancelBtn = new QPushButton("Cancel");
            auto* okBtn     = new QPushButton("OK");
            cancelBtn->setStyleSheet(bs);
            okBtn->setStyleSheet(bs + "QPushButton{font-weight:600;}");
            connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
            connect(okBtn,     &QPushButton::clicked, this, &QDialog::accept);
            row->addStretch(); row->addWidget(cancelBtn); row->addWidget(okBtn);
            root->addLayout(row);
        }

        // Wire callbacks
        m_field->onChanged = [this](float s, float v) {
            m_s=s; m_v=v;
            m_opBar->setColor(selectedColor());
            updatePreviewAndHex();
            if (onColorChanged) onColorChanged(selectedColor(), m_a);
        };
        m_hueBar->onChanged = [this](float h) {
            m_h=h;
            m_field->setHue(h);
            m_opBar->setColor(selectedColor());
            updatePreviewAndHex();
            if (onColorChanged) onColorChanged(selectedColor(), m_a);
        };
        m_opBar->onChanged = [this](float a) {
            m_a=a; updatePreviewAndHex();
            if (onColorChanged) onColorChanged(selectedColor(), m_a);
        };
        connect(m_hexInput, &QLineEdit::editingFinished, m_hexInput, [this]() {
            QString txt = m_hexInput->text().trimmed().remove('#');
            if (txt.length()==6) {
                bool ok; QRgb rgb = txt.toUInt(&ok,16);
                if (ok) {
                    QColor c(rgb); c.getHsvF(&m_h,&m_s,&m_v);
                    if (m_h<0) m_h=0.f;
                    updateAllFromHSV();
                    if (onColorChanged) onColorChanged(selectedColor(), m_a);
                }
            }
        });
    }

    void updateAllFromHSV() {
        m_field->setHue(m_h); m_field->setSV(m_s,m_v);
        m_hueBar->setHue(m_h);
        m_opBar->setAlpha(m_a); m_opBar->setColor(selectedColor());
        updatePreviewAndHex();
    }

    void updatePreviewAndHex() {
        QColor c = selectedColor();
        m_preview->setStyleSheet(QString(
            "background:%1;border:1px solid #5D5D5D;border-radius:6px;").arg(c.name()));
        if (m_hexInput) m_hexInput->setText(c.name().mid(1).toUpper());
    }

    ColorFieldWidget*  m_field    = nullptr;
    HueBarWidget*      m_hueBar   = nullptr;
    OpacityBarWidget*  m_opBar    = nullptr;
    QLabel*            m_preview  = nullptr;
    QLineEdit*         m_hexInput = nullptr;
    float m_h=0.f, m_s=1.f, m_v=1.f, m_a=1.f;
    QPoint m_dragOffset;
};

// ── FillSwatch ───────────────────────────────────────────────

class FillSwatch : public QWidget {
public:
    std::function<void()>      onClicked;
    std::function<void(float)> onOpacityDragged;

    FillSwatch(QColor color, float opacity, QWidget* parent=nullptr)
        : QWidget(parent), m_color(color), m_opacity(opacity)
    { setFixedHeight(34); setCursor(Qt::PointingHandCursor); }

    QColor color()   const { return m_color; }
    float  opacity() const { return m_opacity; }
    void setColor(QColor c)   { m_color=c;                    update(); }
    void setOpacity(float op) { m_opacity=qBound(0.f,op,1.f); update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        p.setPen(QPen(QColor("#5D5D5D"), 1));
        p.setBrush(QColor("#3B3B3B"));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);

        const int h      = height();  // 34
        const int padH   = 7;
        const int sqSize = 20;
        const int sqY    = (h - sqSize) / 2;

        p.setPen(Qt::NoPen);
        p.setBrush(m_color);
        p.drawRoundedRect(QRectF(padH, sqY, sqSize, sqSize), 3, 3);

        QFont f;
        f.setFamilies({"Funnel Display", "Segoe UI", "Arial"});
        f.setPixelSize(13);
        f.setWeight(QFont::Medium);
        p.setFont(f);

        int divX = dividerX();

        p.setPen(QColor("#E3E3E3"));
        p.drawText(QRect(padH + sqSize + 6, 0, divX - (padH + sqSize + 12), h),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   m_color.name().mid(1).toUpper());

        // Vertical divider: 28px tall, centered
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

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button()==Qt::LeftButton) {
            m_pressPos    = e->pos();
            m_dragging    = false;
            m_dragStartOp = m_opacity;
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!(e->buttons() & Qt::LeftButton)) return;
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
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button()==Qt::LeftButton && !m_dragging)
            if (onClicked) onClicked();
        m_dragging = false;
    }

private:
    int dividerX() const { return width() - 55; }

    QColor m_color;
    float  m_opacity;
    bool   m_dragging    = false;
    QPoint m_pressPos;
    float  m_dragStartOp = 1.f;
};

inline DragSpinBox* asDSB(QFrame*  f) { return static_cast<DragSpinBox*>(f); }
inline FillSwatch*  asFS (QWidget* w) { return static_cast<FillSwatch*>(w);  }

} // anonymous namespace

// ============================================================
//  ControlPanel
// ============================================================

ControlPanel::ControlPanel(QWidget* parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    buildLayout();
    m_initializing = false;
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

HalftoneParams ControlPanel::currentParams() const
{
    HalftoneParams p;

    p.shapes.clear();
    for (const auto& slot : m_shapeSlots) {
        ShapeEntry e;
        e.shape   = static_cast<HalftoneShape>(slot.combo->currentIndex());
        e.svgPath = slot.svgPath;
        p.shapes.push_back(e);
    }
    p.multiThreshold = m_sldThreshold->value();

    p.gridSize     = m_sldGrid->value();
    p.gamma        = m_sldGamma->value()   / 100.0f;
    p.jitter       = m_sldJitter->value()  / 100.0f;
    p.symbolSize   = m_sldSize->value()    / 100.0f;
    p.opacity      = asDSB(m_dsbOpacity)->value()      / 100.0f;
    p.cornerRadius = float(asDSB(m_dsbCornerRadius)->value());

    p.useImageColors = m_useImageColors;
    p.fills.clear();
    for (const auto& slot : m_fillSlots) {
        auto* sw = asFS(slot.swatch);
        FillEntry e; e.color=sw->color(); e.opacity=sw->opacity();
        p.fills.push_back(e);
    }

    p.strokeEnabled = m_strokeEnabled;
    p.strokeWidth   = m_sldStrokeWidth->value() / 10.0f;
    p.strokeColor   = m_strokeColor;
    return p;
}

QString ControlPanel::outputFileName() const { return m_edtOutputName->text(); }
QString ControlPanel::outputFormat()   const { return m_cmbFormat->currentText(); }

void ControlPanel::setSourcePreview(const QImage& img)
{
    if (img.isNull()) { m_sourcePreview->setPixmap({}); return; }
    QSize target = m_sourcePreview->size();
    if (target.isEmpty()) target = QSize(260, 130);
    QPixmap pm = QPixmap::fromImage(img)
                     .scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_sourcePreview->setPixmap(pm);
}

// ---------------------------------------------------------------------------
// Build layout
// ---------------------------------------------------------------------------

void ControlPanel::buildLayout()
{
    auto* root = new QWidget(this);
    root->setObjectName("controlRoot");
    auto* vlay = new QVBoxLayout(root);
    vlay->setContentsMargins(14, 16, 14, 20);
    vlay->setSpacing(0);

    // ── SOURCE IMAGE ──────────────────────────────────────────────
    vlay->addWidget(makeSectionHeader("Source image"));
    vlay->addSpacing(10);

    m_sourcePreview = new QLabel;
    m_sourcePreview->setObjectName("sourcePreview");
    m_sourcePreview->setAlignment(Qt::AlignCenter);
    m_sourcePreview->setMinimumHeight(110);
    m_sourcePreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    vlay->addWidget(m_sourcePreview);
    vlay->addSpacing(8);

    {
        auto* btn = new QPushButton;
        btn->setObjectName("uploadBtn");
        btn->setIcon(QIcon(":/icons/upload.svg"));
        btn->setIconSize(QSize(18,19));
        btn->setText("  Upload image");
        btn->setFixedHeight(36);
        connect(btn, &QPushButton::clicked, this, &ControlPanel::fileRequested);
        vlay->addWidget(btn);
    }

    vlay->addSpacing(16);
    vlay->addWidget(makeSeparator());
    vlay->addSpacing(16);

    // ── HALFTONE SHAPE ────────────────────────────────────────────
    QPushButton* shapePlusBtn = nullptr;
    vlay->addWidget(makeSectionHeader("Halftone shape", &shapePlusBtn));
    vlay->addSpacing(10);

    m_shapesContainer = new QWidget;
    m_shapesLayout    = new QVBoxLayout(m_shapesContainer);
    m_shapesLayout->setContentsMargins(0,0,0,0);
    m_shapesLayout->setSpacing(6);
    vlay->addWidget(m_shapesContainer);

    {
        m_thresholdRow = new QWidget;
        auto* tl = new QVBoxLayout(m_thresholdRow);
        tl->setContentsMargins(0,10,0,0); tl->setSpacing(4);
        auto* tLbl = new QLabel("Threshold"); tLbl->setObjectName("paramLabel");
        m_sldThreshold = new NoWheelSlider(Qt::Horizontal);
        m_sldThreshold->setRange(0,255); m_sldThreshold->setValue(128);
        tl->addWidget(tLbl); tl->addWidget(m_sldThreshold);
        m_thresholdRow->setVisible(false);
        vlay->addWidget(m_thresholdRow);
    }

    addShapeSlot();

    connect(shapePlusBtn, &QPushButton::clicked, this, [this]() {
        if (m_shapeSlots.size() < 4) addShapeSlot();
    });
    connect(m_sldThreshold, &QSlider::valueChanged, this, [this]() {
        if (!m_initializing) emit paramsChanged();
    });

    vlay->addSpacing(16);
    vlay->addWidget(makeSeparator());
    vlay->addSpacing(16);

    // ── PARAMETERS ───────────────────────────────────────────────
    vlay->addWidget(makeSectionHeader("Parameters"));
    vlay->addSpacing(10);

    vlay->addWidget(make2ColSliders("Grid",  &m_sldGrid,   2,  100,  20,
                                    "Gamma", &m_sldGamma,  10, 500, 100));
    vlay->addSpacing(12);
    vlay->addWidget(make2ColSliders("Jitter",&m_sldJitter,  0, 100,   0,
                                    "Size",  &m_sldSize,   10, 300, 100));
    vlay->addSpacing(10);

    {
        auto* row = new QHBoxLayout; row->setContentsMargins(0,0,0,0); row->setSpacing(8);
        auto* dsbOp = new DragSpinBox(":/icons/opacity.svg",       0, 100, 100, "%");
        auto* dsbCr = new DragSpinBox(":/icons/corner_radius.svg", 0,  50,   0, "");
        m_dsbOpacity = dsbOp; m_dsbCornerRadius = dsbCr;
        row->addWidget(dsbOp,1); row->addWidget(dsbCr,1);
        auto* w = new QWidget; w->setLayout(row); w->setFixedHeight(34); vlay->addWidget(w);
        dsbOp->onValueChanged = [this](int){ if (!m_initializing) emit paramsChanged(); };
        dsbCr->onValueChanged = [this](int){ if (!m_initializing) emit paramsChanged(); };
    }

    auto csld = [&](QSlider* s) {
        connect(s, &QSlider::valueChanged, this, [this]() { if (!m_initializing) emit paramsChanged(); });
    };
    csld(m_sldGrid); csld(m_sldGamma); csld(m_sldJitter); csld(m_sldSize);

    vlay->addSpacing(16);
    vlay->addWidget(makeSeparator());
    vlay->addSpacing(16);

    // ── FILL ─────────────────────────────────────────────────────
    // Custom header row: "Fill" | [Use image colors] | [+]
    {
        auto* hdr    = new QWidget;
        auto* hdrLay = new QHBoxLayout(hdr);
        hdrLay->setContentsMargins(0,0,0,0); hdrLay->setSpacing(6);

        auto* titleLbl = new QLabel("Fill"); titleLbl->setObjectName("sectionTitle");
        hdrLay->addWidget(titleLbl, 1);

        // "Use image colors" toggle chip
        m_useImageColorsBtn = new QPushButton("Use image colors");
        m_useImageColorsBtn->setCheckable(true);
        m_useImageColorsBtn->setFixedHeight(22);
        m_useImageColorsBtn->setStyleSheet(
            "QPushButton{background:#3B3B3B;border:1px solid #5D5D5D;border-radius:4px;"
            "color:#B2B2B2;font-size:8pt;padding:0 8px;}"
            "QPushButton:checked{background:#484848;border-color:#828282;color:#E3E3E3;}"
            "QPushButton:hover{border-color:#828282;}");
        hdrLay->addWidget(m_useImageColorsBtn);

        m_fillPlusBtn = makeIconButton(":/icons/plus.svg");
        hdrLay->addWidget(m_fillPlusBtn);
        vlay->addWidget(hdr);
    }
    vlay->addSpacing(10);

    m_fillsContainer = new QWidget;
    m_fillsLayout    = new QVBoxLayout(m_fillsContainer);
    m_fillsLayout->setContentsMargins(0,0,0,0);
    m_fillsLayout->setSpacing(6);
    vlay->addWidget(m_fillsContainer);

    addFillSlot();

    connect(m_fillPlusBtn, &QPushButton::clicked, this, [this]() { addFillSlot(); });
    connect(m_useImageColorsBtn, &QPushButton::toggled, this, [this](bool on) {
        m_useImageColors = on;
        m_fillsContainer->setVisible(!on);
        m_fillPlusBtn->setVisible(!on);
        if (!m_initializing) emit paramsChanged();
    });

    vlay->addSpacing(16);
    vlay->addWidget(makeSeparator());
    vlay->addSpacing(16);

    // ── STROKE ────────────────────────────────────────────────────
    {
        auto* hdr    = new QWidget;
        auto* hdrLay = new QHBoxLayout(hdr);
        hdrLay->setContentsMargins(0,0,0,0); hdrLay->setSpacing(6);
        auto* titleLbl    = new QLabel("Stroke"); titleLbl->setObjectName("sectionTitle");
        m_strokeToggleBtn = makeIconButton(":/icons/plus.svg");
        hdrLay->addWidget(titleLbl,1); hdrLay->addWidget(m_strokeToggleBtn);
        vlay->addWidget(hdr);
    }

    m_strokeContent = new QWidget;
    {
        auto* sl = new QVBoxLayout(m_strokeContent);
        sl->setContentsMargins(0,10,0,0); sl->setSpacing(6);
        auto* wLbl = new QLabel("Stroke Width"); wLbl->setObjectName("paramLabel");
        m_sldStrokeWidth = new NoWheelSlider(Qt::Horizontal);
        m_sldStrokeWidth->setRange(1,200); m_sldStrokeWidth->setValue(10);
        sl->addWidget(wLbl); sl->addWidget(m_sldStrokeWidth); sl->addSpacing(4);

        auto* strokeSwatch = new FillSwatch(m_strokeColor, 1.0f);
        m_strokeColorBtn = strokeSwatch;
        sl->addWidget(m_strokeColorBtn);

        connect(m_sldStrokeWidth, &QSlider::valueChanged, this, [this]() {
            if (!m_initializing) emit paramsChanged();
        });
        strokeSwatch->onClicked = [this]() {
            auto* sw = asFS(m_strokeColorBtn);
            const QColor origColor = sw->color();

            auto* dlg = new ColorPickerDialog(sw->color(), 1.0f, this);
            QPoint gp = m_strokeColorBtn->mapToGlobal(QPoint(0, 0));
            QRect sg  = m_strokeColorBtn->screen()->availableGeometry();
            int dlgX  = qMax(sg.left(), gp.x() - dlg->width() - 8);
            int dlgY  = qBound(sg.top(), gp.y(), sg.bottom() - dlg->height());
            dlg->move(dlgX, dlgY);

            dlg->onColorChanged = [this](QColor c, float) {
                asFS(m_strokeColorBtn)->setColor(c);
                m_strokeColor = c;
                if (!m_initializing) emit paramsChanged();
            };

            if (dlg->exec() == QDialog::Accepted) {
                m_strokeColor = dlg->selectedColor();
                asFS(m_strokeColorBtn)->setColor(m_strokeColor);
                if (!m_initializing) emit paramsChanged();
            } else {
                asFS(m_strokeColorBtn)->setColor(origColor);
                m_strokeColor = origColor;
                if (!m_initializing) emit paramsChanged();
            }
            dlg->deleteLater();
        };
    }
    m_strokeContent->setVisible(false);
    vlay->addWidget(m_strokeContent);

    connect(m_strokeToggleBtn, &QPushButton::clicked, this, [this]() {
        m_strokeEnabled = !m_strokeEnabled;
        updateStrokeUI();
        if (!m_initializing) emit paramsChanged();
    });

    vlay->addSpacing(16);
    vlay->addWidget(makeSeparator());
    vlay->addSpacing(16);

    // ── EXPORT ───────────────────────────────────────────────────
    vlay->addWidget(makeSectionHeader("Export"));
    vlay->addSpacing(10);

    {
        auto* labelsRow = new QHBoxLayout; labelsRow->setContentsMargins(0,0,0,0); labelsRow->setSpacing(8);
        auto* nameLbl = new QLabel("Output name"); nameLbl->setObjectName("paramLabel");
        auto* typeLbl = new QLabel("Type of file"); typeLbl->setObjectName("paramLabel");
        labelsRow->addWidget(nameLbl,2); labelsRow->addWidget(typeLbl,1);
        auto* lw = new QWidget; lw->setLayout(labelsRow); vlay->addWidget(lw);
        vlay->addSpacing(4);

        auto* fieldsRow = new QHBoxLayout; fieldsRow->setContentsMargins(0,0,0,0); fieldsRow->setSpacing(8);
        m_edtOutputName = new QLineEdit("output");
        m_cmbFormat     = new NoWheelComboBox;
        m_cmbFormat->addItems({"SVG","PNG","JPG"});
        fieldsRow->addWidget(m_edtOutputName,2); fieldsRow->addWidget(m_cmbFormat,1);
        auto* fw = new QWidget; fw->setLayout(fieldsRow); vlay->addWidget(fw);
        vlay->addSpacing(10);

        auto* btnExport = new QPushButton("Export");
        btnExport->setObjectName("exportBtn");
        btnExport->setFixedHeight(40);
        connect(btnExport, &QPushButton::clicked, this, &ControlPanel::exportRequested);
        vlay->addWidget(btnExport);
    }

    vlay->addStretch();
    setWidget(root);
}

// ---------------------------------------------------------------------------
// Section helpers
// ---------------------------------------------------------------------------

QWidget* ControlPanel::makeSectionHeader(const QString& title, QPushButton** plusOut)
{
    auto* w   = new QWidget;
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0,0,0,0); lay->setSpacing(6);
    auto* lbl = new QLabel(title); lbl->setObjectName("sectionTitle");
    lay->addWidget(lbl,1);
    if (plusOut) { *plusOut = makeIconButton(":/icons/plus.svg"); lay->addWidget(*plusOut); }
    return w;
}

QFrame* ControlPanel::makeSeparator()
{
    auto* f = new QFrame;
    f->setObjectName("separator");
    f->setFrameShape(QFrame::NoFrame);
    f->setFixedHeight(1);
    return f;
}

QPushButton* ControlPanel::makeIconButton(const QString& iconRes)
{
    auto* btn = new QPushButton;
    btn->setObjectName("iconBtn");
    btn->setIcon(QIcon(iconRes));
    btn->setIconSize(QSize(14,14));
    btn->setFixedSize(24,24);
    return btn;
}

QWidget* ControlPanel::makeSliderCol(const QString& lbl, QSlider** sldOut,
                                      int minVal, int maxVal, int defVal)
{
    auto* w  = new QWidget;
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0,0,0,0); vl->setSpacing(4);
    auto* label = new QLabel(lbl); label->setObjectName("paramLabel");
    auto* sld   = new NoWheelSlider(Qt::Horizontal);
    sld->setRange(minVal,maxVal); sld->setValue(defVal);
    vl->addWidget(label); vl->addWidget(sld);
    *sldOut = sld;
    return w;
}

QWidget* ControlPanel::make2ColSliders(
    const QString& lbl1, QSlider** sld1, int min1, int max1, int def1,
    const QString& lbl2, QSlider** sld2, int min2, int max2, int def2)
{
    auto* w  = new QWidget;
    auto* hl = new QHBoxLayout(w);
    hl->setContentsMargins(0,0,0,0); hl->setSpacing(12);
    hl->addWidget(makeSliderCol(lbl1,sld1,min1,max1,def1),1);
    hl->addWidget(makeSliderCol(lbl2,sld2,min2,max2,def2),1);
    return w;
}

// ---------------------------------------------------------------------------
// Shape slots
// ---------------------------------------------------------------------------

void ControlPanel::addShapeSlot(HalftoneShape shape, const QString& svgPath)
{
    if (m_shapeSlots.size() >= 4) return;

    ShapeSlot slot;
    slot.widget = new QWidget;
    auto* outer = new QVBoxLayout(slot.widget);
    outer->setContentsMargins(0,0,0,0); outer->setSpacing(4);

    auto* mainRow = new QHBoxLayout;
    mainRow->setContentsMargins(0,0,0,0); mainRow->setSpacing(6);

    slot.combo = new NoWheelComboBox;
    slot.combo->addItems({"Triangle","Circle","Square","Star","Custom SVG"});
    slot.combo->setCurrentIndex(static_cast<int>(shape));
    slot.minusBtn = makeIconButton(":/icons/minus.svg");
    slot.svgPath  = svgPath;

    mainRow->addWidget(slot.combo,1); mainRow->addWidget(slot.minusBtn);

    slot.svgRow = new QWidget;
    {
        auto* svgLay = new QHBoxLayout(slot.svgRow);
        svgLay->setContentsMargins(0,0,30,0);
        slot.svgBtn = new QPushButton;
        slot.svgBtn->setObjectName("uploadBtn");
        slot.svgBtn->setFixedHeight(34);
        if (svgPath.isEmpty()) {
            slot.svgBtn->setIcon(QIcon(":/icons/upload.svg"));
            slot.svgBtn->setIconSize(QSize(16,17));
            slot.svgBtn->setText("  Upload SVG");
        } else {
            slot.svgBtn->setText("  Change SVG");
            const int ci = static_cast<int>(HalftoneShape::CustomSVG);
            slot.combo->setItemIcon(ci, QIcon(svgPath));
            slot.combo->setItemText(ci, "  " + QFileInfo(svgPath).baseName());
        }
        svgLay->addWidget(slot.svgBtn);
    }
    slot.svgRow->setVisible(shape == HalftoneShape::CustomSVG);

    outer->addLayout(mainRow); outer->addWidget(slot.svgRow);

    m_shapeSlots.append(slot);
    m_shapesLayout->addWidget(slot.widget);

    for (auto& s : m_shapeSlots) s.minusBtn->setEnabled(m_shapeSlots.size() > 1);

    QWidget*     slotWidget = slot.widget;
    QWidget*     svgRow     = slot.svgRow;
    QPushButton* svgBtn     = slot.svgBtn;

    connect(slot.minusBtn, &QPushButton::clicked, this, [this,slotWidget]() { removeShapeSlot(slotWidget); });
    connect(slot.combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this,svgRow](int idx) {
        svgRow->setVisible(idx == static_cast<int>(HalftoneShape::CustomSVG));
        if (!m_initializing) emit paramsChanged();
    });
    QComboBox* combo = slot.combo;
    connect(svgBtn, &QPushButton::clicked, this, [this, slotWidget, svgBtn, combo]() {
        QString path = QFileDialog::getOpenFileName(this, "Load SVG", "", "SVG files (*.svg)");
        if (!path.isEmpty()) {
            for (auto& s : m_shapeSlots) if (s.widget == slotWidget) { s.svgPath = path; break; }
            const int ci = static_cast<int>(HalftoneShape::CustomSVG);
            combo->setItemIcon(ci, QIcon(path));
            combo->setItemText(ci, "  " + QFileInfo(path).baseName());
            svgBtn->setIcon(QIcon());
            svgBtn->setText("  Change SVG");
            if (!m_initializing) emit paramsChanged();
        }
    });

    refreshThresholdVisibility();
    if (!m_initializing) emit paramsChanged();
}

void ControlPanel::removeShapeSlot(QWidget* slotWidget)
{
    if (m_shapeSlots.size() <= 1) return;
    int idx = -1;
    for (int i=0;i<m_shapeSlots.size();++i)
        if (m_shapeSlots[i].widget==slotWidget) { idx=i; break; }
    if (idx<0) return;
    m_shapesLayout->removeWidget(slotWidget);
    slotWidget->deleteLater();
    m_shapeSlots.removeAt(idx);
    for (auto& s : m_shapeSlots) s.minusBtn->setEnabled(m_shapeSlots.size() > 1);
    refreshThresholdVisibility();
    if (!m_initializing) emit paramsChanged();
}

void ControlPanel::refreshThresholdVisibility()
{
    m_thresholdRow->setVisible(m_shapeSlots.size() > 1);
    if (!m_shapeSlots.isEmpty())
        m_shapeSlots[0].minusBtn->setEnabled(m_shapeSlots.size() > 1);
}

// ---------------------------------------------------------------------------
// Fill slots
// ---------------------------------------------------------------------------

void ControlPanel::addFillSlot(QColor color, float opacity)
{
    FillSlot slot;
    slot.widget   = new QWidget;
    auto* row     = new QHBoxLayout(slot.widget);
    row->setContentsMargins(0,0,0,0); row->setSpacing(6);

    auto* sw      = new FillSwatch(color, opacity);
    slot.swatch   = sw;
    slot.minusBtn = makeIconButton(":/icons/minus.svg");

    row->addWidget(sw,1); row->addWidget(slot.minusBtn);

    m_fillSlots.append(slot);
    m_fillsLayout->addWidget(slot.widget);

    for (auto& s : m_fillSlots) s.minusBtn->setEnabled(m_fillSlots.size() > 1);

    QWidget* slotWidget = slot.widget;

    sw->onClicked = [this,slotWidget]() {
        for (int i=0;i<m_fillSlots.size();++i)
            if (m_fillSlots[i].widget==slotWidget) { openColorPicker(i); break; }
    };
    sw->onOpacityDragged = [this](float) { if (!m_initializing) emit paramsChanged(); };
    connect(slot.minusBtn, &QPushButton::clicked, this, [this,slotWidget]() { removeFillSlot(slotWidget); });

    if (!m_initializing) emit paramsChanged();
}

void ControlPanel::removeFillSlot(QWidget* slotWidget)
{
    if (m_fillSlots.size() <= 1) return;
    int idx=-1;
    for (int i=0;i<m_fillSlots.size();++i)
        if (m_fillSlots[i].widget==slotWidget) { idx=i; break; }
    if (idx<0) return;
    m_fillsLayout->removeWidget(slotWidget);
    slotWidget->deleteLater();
    m_fillSlots.removeAt(idx);
    for (auto& s : m_fillSlots) s.minusBtn->setEnabled(m_fillSlots.size() > 1);
    if (!m_initializing) emit paramsChanged();
}

void ControlPanel::openColorPicker(int idx)
{
    if (idx<0 || idx>=m_fillSlots.size()) return;
    auto* sw  = asFS(m_fillSlots[idx].swatch);

    const QColor origColor   = sw->color();
    const float  origOpacity = sw->opacity();

    auto* dlg = new ColorPickerDialog(sw->color(), sw->opacity(), this);
    QPoint gp = sw->mapToGlobal(QPoint(0, 0));
    QRect sg  = sw->screen()->availableGeometry();
    int dlgX  = qMax(sg.left(), gp.x() - dlg->width() - 8);
    int dlgY  = qBound(sg.top(), gp.y(), sg.bottom() - dlg->height());
    dlg->move(dlgX, dlgY);

    dlg->onColorChanged = [this, idx](QColor c, float a) {
        if (idx < m_fillSlots.size()) {
            asFS(m_fillSlots[idx].swatch)->setColor(c);
            asFS(m_fillSlots[idx].swatch)->setOpacity(a);
            if (!m_initializing) emit paramsChanged();
        }
    };

    if (dlg->exec() != QDialog::Accepted) {
        sw->setColor(origColor);
        sw->setOpacity(origOpacity);
        if (!m_initializing) emit paramsChanged();
    }
    dlg->deleteLater();
}

// ---------------------------------------------------------------------------
// Stroke
// ---------------------------------------------------------------------------

void ControlPanel::updateStrokeUI()
{
    m_strokeContent->setVisible(m_strokeEnabled);
    m_strokeToggleBtn->setIcon(QIcon(m_strokeEnabled ? ":/icons/minus.svg" : ":/icons/plus.svg"));
}

void ControlPanel::updateStrokeColorBtn()
{
    asFS(m_strokeColorBtn)->setColor(m_strokeColor);
}
