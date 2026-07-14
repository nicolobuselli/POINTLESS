#include "ControlsPanel.h"
#include "AdjustmentsPanel.h"
#include "LayersPanel.h"
#include "Widgets.h"
#include "UiScale.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QDoubleValidator>
#include <QSignalBlocker>
#include <QIcon>
#include <cmath>

namespace {

// Full-width 1px divider used to bracket the section titles.
QFrame* bandLine()
{
    auto* f = new QFrame;
    f->setObjectName("bandLine");
    f->setFixedHeight(1);
    return f;
}

// A fixed section-title band: title text in a row with vertical padding.
QWidget* titleBand(const QString& text)
{
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(Ui::px(40), Ui::px(12), Ui::px(40), Ui::px(12));
    l->setSpacing(0);
    l->addWidget(makeSectionTitle(text), 1);
    return w;
}

// Scale slider: log mapping over [0.1x .. 10x], centred at 1.0x.
// slider 0..1000 → multiplier; 500 == 1.0x.
constexpr int kScaleSliderMax = 1000;
double sliderToMult(int s) { return 0.1 * std::pow(100.0, double(s) / kScaleSliderMax); }
int    multToSlider(double m) {
    m = qBound(0.1, m, 10.0);
    return qRound(kScaleSliderMax * std::log10(m / 0.1) / 2.0);   // log10(100)=2
}
// Trim a multiplier to a tidy string: "1", "1.5", "0.2".
QString fmtMult(double m) {
    QString s = QString::number(m, 'f', 1);
    if (s.contains('.')) { while (s.endsWith('0')) s.chop(1); if (s.endsWith('.')) s.chop(1); }
    return s;
}

} // namespace

ControlsPanel::ControlsPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("sidePanel");
    setMinimumWidth(Ui::px(420));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── File name (editable, click to rename, Enter confirms) ────
    {
        auto* nameRow = new QWidget;
        auto* nl = new QHBoxLayout(nameRow);
        nl->setContentsMargins(Ui::px(40), Ui::px(14), Ui::px(20), Ui::px(14));   // aligned with "Layers"
        nl->setSpacing(0);

        m_fileTitle = new QLineEdit;
        m_fileTitle->setObjectName("fileTitleEdit");
        m_fileTitle->setFrame(false);
        m_fileTitle->setPlaceholderText("add title…");
        m_fileTitle->setFocusPolicy(Qt::ClickFocus);   // no caret until clicked
        connect(m_fileTitle, &QLineEdit::returnPressed, this, [this]() {
            m_fileTitle->clearFocus();
        });
        connect(m_fileTitle, &QLineEdit::editingFinished, this, [this]() {
            if (!m_updating) emit fileRenamed(m_fileTitle->text().trimmed());
        });
        nl->addWidget(m_fileTitle, 1);
        outer->addWidget(nameRow);
    }

    outer->addWidget(bandLine());

    // Layers / Parameters are split by a draggable handle so the user can
    // decide how much height each gets.
    auto* split = new QSplitter(Qt::Vertical);
    split->setObjectName("leftSplit");
    split->setChildrenCollapsible(false);
    split->setHandleWidth(Ui::px(1));   // 1px, coherent with the bandLine dividers

    // ── Layers pane (header + scrollable embedded list) ──────────
    auto* layersPane = new QWidget;
    {
        auto* ll = new QVBoxLayout(layersPane);
        ll->setContentsMargins(0, 0, 0, 0);
        ll->setSpacing(0);

        // "Layers" header with a "+" in the gutter to add a layer (a new layer
        // of the current mode, or a duplicate of the active one).
        {
            auto* hdr = new QWidget;
            // Title band: fixed height so an empty layer list lets the list area
            // (stretch) absorb the slack — otherwise the header floats mid-pane.
            hdr->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            auto* hl = new QHBoxLayout(hdr);
            hl->setContentsMargins(Ui::px(40), Ui::px(12), 0, Ui::px(12));
            hl->setSpacing(0);
            hl->addWidget(makeSectionTitle("Layers"), 1);
            // "+" centred in a 70px gutter so it lines up with the layer eyes.
            auto* gut = new QWidget;
            gut->setFixedWidth(Ui::px(70));
            auto* gl = new QHBoxLayout(gut);
            gl->setContentsMargins(0, 0, 0, 0);
            auto* add = new QPushButton;
            add->setObjectName("iconBtn");
            add->setCursor(Qt::PointingHandCursor);
            add->setFixedSize(Ui::px(26), Ui::px(26));
            add->setIcon(QIcon(":/icons/plus.svg"));
            add->setIconSize(QSize(Ui::px(16), Ui::px(16)));
            add->setToolTip("Add layer");
            connect(add, &QPushButton::clicked, this, [this]() { m_layers->requestAddLayer(); });
            gl->addWidget(add, 0, Qt::AlignCenter);
            m_addLayerBtn = add;
            hl->addWidget(gut);
            ll->addWidget(hdr);
        }
        m_layers = new LayersPanel(/*embedded*/ true);
        m_layers->setObjectName("layersEmbedded");
        m_layers->setMinimumHeight(Ui::px(64));   // at least ~1 row, then scroll
        ll->addWidget(m_layers, 1);

        // Frame dimensions (W × H of the canvas) live at the bottom of Layers.
        auto* frameBox = new QWidget;
        auto* fb = new QVBoxLayout(frameBox);
        fb->setContentsMargins(Ui::px(40), Ui::px(10), Ui::px(70), Ui::px(12));
        fb->setSpacing(Ui::px(8));
        fb->addWidget(makeParamLabel("Frame dimensions"));
        {
            auto* row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(Ui::px(12));
            m_frameW = new DragSpinBox("", 16, 8192, 1080);
            m_frameH = new DragSpinBox("", 16, 8192, 1080);
            m_frameW->setTextLabel("W");
            m_frameH->setTextLabel("H");
            auto emitFrame = [this](int) {
                if (!m_updating) emit frameSizeChanged(m_frameW->value(), m_frameH->value());
            };
            m_frameW->onValueChanged = emitFrame;
            m_frameH->onValueChanged = emitFrame;
            row->addWidget(m_frameW, 1);
            row->addWidget(m_frameH, 1);
            fb->addLayout(row);
        }
        ll->addWidget(frameBox);
    }

    // ── Transform pane (Frame dimensions + active-layer placement) ───
    auto* transformPane = new QWidget;
    {
        auto* tp = new QVBoxLayout(transformPane);
        tp->setContentsMargins(0, 0, 0, 0);
        tp->setSpacing(0);
        tp->addWidget(bandLine());            // line above the title
        tp->addWidget(titleBand("Transform"));

        auto* box = new QWidget;
        auto* bl = new QVBoxLayout(box);
        // Match the Parameters rows exactly: 40px left, 70px right gutter, so
        // every functional row in the column has the same width.
        bl->setContentsMargins(Ui::px(40), Ui::px(12), Ui::px(70), Ui::px(16));
        bl->setSpacing(Ui::px(8));

        auto emitTf = [this](int) { if (!m_updating) emitTransform(); };

        // ── Position (X / Y px from frame centre) ──
        // label+row stacked tight (px2) — same label→control gap as Scale.
        {
            auto* grp = new QVBoxLayout;
            grp->setContentsMargins(0, 0, 0, 0);
            grp->setSpacing(Ui::px(2));
            { auto* l = makeParamLabel("Position"); l->setObjectName("sliderLabel"); grp->addWidget(l); }
            auto* posRow = new QHBoxLayout;
            posRow->setContentsMargins(0, 0, 0, 0);
            posRow->setSpacing(Ui::px(12));
            m_tfX = new DragSpinBox("", -8192, 8192, 0);
            m_tfY = new DragSpinBox("", -8192, 8192, 0);
            m_tfX->setTextLabel("X");
            m_tfY->setTextLabel("Y");
            m_tfX->onValueChanged = emitTf;
            m_tfY->onValueChanged = emitTf;
            posRow->addWidget(m_tfX, 1);
            posRow->addWidget(m_tfY, 1);
            grp->addLayout(posRow);
            bl->addLayout(grp);
        }

        // ── Rotation (angle box + a box of quick-transform buttons) ──
        // label+row stacked tight (px2) — same label→control gap as Scale.
        auto* rotGrp = new QVBoxLayout;
        rotGrp->setContentsMargins(0, 0, 0, 0);
        rotGrp->setSpacing(Ui::px(2));
        { auto* l = makeParamLabel("Rotation"); l->setObjectName("sliderLabel"); rotGrp->addWidget(l); }
        {
            auto* rotRow = new QHBoxLayout;
            rotRow->setContentsMargins(0, 0, 0, 0);
            rotRow->setSpacing(Ui::px(12));

            m_tfRot = new DragSpinBox(":/icons/rotation.svg", -180, 180, 0, QStringLiteral("°"));
            m_tfRot->onValueChanged = emitTf;
            rotRow->addWidget(m_tfRot, 1);

            // Quick-transform box: rotate 90°, mirror-x, mirror-y — icons spread
            // evenly across the box so it fills the row's right half.
            auto* quick = new QFrame;
            quick->setObjectName("dragSpinBox");
            quick->setFixedHeight(Ui::px(48));
            auto* ql = new QHBoxLayout(quick);
            ql->setContentsMargins(Ui::px(8), 0, Ui::px(8), 0);
            ql->setSpacing(0);

            auto makeQuickBtn = [&](const QString& icon, const QString& tip, bool checkable) {
                auto* b = new QPushButton(quick);
                b->setObjectName("iconBtn");
                b->setCheckable(checkable);
                b->setCursor(Qt::PointingHandCursor);
                b->setFixedSize(Ui::px(34), Ui::px(34));
                b->setIcon(QIcon(icon));
                b->setIconSize(QSize(Ui::px(18), Ui::px(18)));
                b->setToolTip(tip);
                ql->addStretch(1);
                ql->addWidget(b);
                ql->addStretch(1);
                return b;
            };

            auto* rot90 = makeQuickBtn(":/icons/rotate90.svg", "Rotate 90°", false);
            connect(rot90, &QPushButton::clicked, this, [this]() {
                if (m_updating) return;
                int r = m_tfRot->value() + 90;
                while (r >  180) r -= 360;
                while (r < -180) r += 360;
                m_updating = true; m_tfRot->setValue(r); m_updating = false;
                emitTransform();
            });
            m_flipH = makeQuickBtn(":/icons/mirror_x.svg", "Mirror (y axis)", true);
            m_flipV = makeQuickBtn(":/icons/mirror_y.svg", "Mirror (x axis)", true);
            connect(m_flipH, &QPushButton::toggled, this, [this](bool) { if (!m_updating) emitTransform(); });
            connect(m_flipV, &QPushButton::toggled, this, [this](bool) { if (!m_updating) emitTransform(); });

            rotRow->addWidget(quick, 1);
            rotGrp->addLayout(rotRow);
            bl->addLayout(rotGrp);
        }

        // ── Scale (log slider 0.1×..10×, centred at 1×, + free-form box) ──
        // Mirror SliderRow: label+slider stacked tight on the left, box (58×46)
        // centred over the whole block on the right — so it matches Parameters.
        {
            auto* scRow = new QHBoxLayout;
            scRow->setContentsMargins(0, 0, 0, 0);
            scRow->setSpacing(Ui::px(12));

            auto* leftCol = new QVBoxLayout;
            leftCol->setContentsMargins(0, 0, 0, 0);
            leftCol->setSpacing(Ui::px(2));
            { auto* l = makeParamLabel("Scale"); l->setObjectName("sliderLabel"); leftCol->addWidget(l); }

            m_tfScaleSlider = new NoWheelSlider(Qt::Horizontal);
            m_tfScaleSlider->setRange(0, kScaleSliderMax);
            m_tfScaleSlider->setValue(kScaleSliderMax / 2);   // 1.0×
            m_tfScaleSlider->setFixedHeight(Ui::px(30));
            leftCol->addWidget(m_tfScaleSlider);

            m_tfScaleEdit = new QLineEdit("1");
            m_tfScaleEdit->setObjectName("dragSpinBox");   // box bg/border/hover
            m_tfScaleEdit->setAlignment(Qt::AlignCenter);
            m_tfScaleEdit->setFixedSize(Ui::px(58), Ui::px(46));   // == Parameters cell
            // Match the DragSpinBox value text and pin the height so the generic
            // QLineEdit rule (different colour/size + min-height) can't reshape it.
            m_tfScaleEdit->setStyleSheet(QString(
                "color:#A6A6A6; font-size:%1px; font-weight:500; padding:0;"
                " min-height:%2px; max-height:%2px;")
                .arg(Ui::px(19)).arg(Ui::px(46)));
            auto* val = new QDoubleValidator(0.001, 1000.0, 3, m_tfScaleEdit);
            val->setNotation(QDoubleValidator::StandardNotation);
            m_tfScaleEdit->setValidator(val);

            scRow->addLayout(leftCol, 1);
            scRow->addWidget(m_tfScaleEdit, 0, Qt::AlignVCenter);
            bl->addLayout(scRow);

            // Slider drag → box follows, emit.
            connect(m_tfScaleSlider, &QSlider::valueChanged, this, [this](int s) {
                if (m_updating) return;
                m_updating = true;
                m_tfScaleEdit->setText(fmtMult(sliderToMult(s)));
                m_updating = false;
                emitTransform();
            });
            // Typed value → can exceed slider ends; slider clamps for display only.
            connect(m_tfScaleEdit, &QLineEdit::editingFinished, this, [this]() {
                if (m_updating) return;
                const double m = qMax(0.001, m_tfScaleEdit->text().toDouble());
                m_updating = true;
                m_tfScaleEdit->setText(fmtMult(m));
                QSignalBlocker bl(m_tfScaleSlider);
                m_tfScaleSlider->setValue(multToSlider(m));
                m_updating = false;
                emitTransform();
            });
        }
        tp->addWidget(box);
    }

    // ── Parameters pane (header + scrollable adjustments) ────────
    auto* paramsPane = new QWidget;
    {
        auto* pp = new QVBoxLayout(paramsPane);
        pp->setContentsMargins(0, 0, 0, 0);
        pp->setSpacing(0);
        // Transform sits at the top of this pane so it stays glued to Parameters
        // (a single splitter handle separates them from Layers).
        pp->addWidget(transformPane);
        pp->addWidget(bandLine());            // line above the title
        pp->addWidget(titleBand("Adjustments"));
        m_adjust = new AdjustmentsPanel;
        connect(m_adjust, &AdjustmentsPanel::adjustmentsChanged,
                this, &ControlsPanel::adjustmentsChanged);
        connect(m_adjust, &AdjustmentsPanel::resetRequested,
                this, &ControlsPanel::resetRequested);
        pp->addWidget(m_adjust, 1);
        // Let the Parameters scroll shrink, but never the Transform block above
        // it: the pane's minimum then = Transform (fixed) + this floor, so
        // dragging the divider down can't squash the Transform boxes.
        m_adjust->setMinimumHeight(Ui::px(90));
    }

    split->addWidget(layersPane);
    split->addWidget(paramsPane);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    // Default: room for ~4 rows — halves the empty gap below the startup layers
    // (parent + one child) before Transform; the user can still drag the divider.
    split->setSizes({ Ui::px(4 * 52 + 3 * 6 + 90), Ui::px(760) });
    outer->addWidget(split, 1);
}

void ControlsPanel::setFileName(const QString& name)
{
    m_updating = true;
    m_fileTitle->setText(name);          // empty → "add title…" placeholder
    m_fileTitle->setCursorPosition(0);
    m_updating = false;
}

void ControlsPanel::setAddLayerVisible(bool on)
{
    if (m_addLayerBtn) m_addLayerBtn->setVisible(on);
}

void ControlsPanel::setFrameSize(int w, int h)
{
    m_curFrameW = w > 0 ? w : 1080;
    m_curFrameH = h > 0 ? h : 1080;
    m_updating = true;
    if (m_frameW) m_frameW->setValue(w);
    if (m_frameH) m_frameH->setValue(h);
    m_updating = false;
}

void ControlsPanel::setTransform(const LayerTransform& t)
{
    m_updating = true;
    if (m_tfX) m_tfX->setValue(qRound(t.xPct * m_curFrameW));
    if (m_tfY) m_tfY->setValue(qRound(t.yPct * m_curFrameH));
    if (m_tfRot) {
        float r = std::fmod(t.rotation, 360.0f);
        if (r > 180.0f)  r -= 360.0f;
        if (r < -180.0f) r += 360.0f;
        m_tfRot->setValue(qRound(r));
    }
    setScale(t.scalePct);
    if (m_flipH) m_flipH->setChecked(t.flipH);
    if (m_flipV) m_flipV->setChecked(t.flipV);
    m_updating = false;
}

namespace {
void tintBox(QWidget* w, bool on)
{
    if (!w) return;
    w->setProperty("animated", on);
    w->style()->unpolish(w);
    w->style()->polish(w);
}
}

void ControlsPanel::setAnimatedParams(const QSet<ParamId>& ids)
{
    m_adjust->setAnimatedParams(ids);
    if (m_tfX) m_tfX->setAnimated(ids.contains(ParamId::TfX));
    if (m_tfY) m_tfY->setAnimated(ids.contains(ParamId::TfY));
    if (m_tfRot) m_tfRot->setAnimated(ids.contains(ParamId::TfRotation));
    tintBox(m_tfScaleEdit, ids.contains(ParamId::TfScale));   // plain QLineEdit, not a DragSpinBox
}

QHash<QWidget*, ParamId> ControlsPanel::paramWidgets() const
{
    QHash<QWidget*, ParamId> m = m_adjust->paramWidgets();
    if (m_tfX) m.insert(m_tfX, ParamId::TfX);
    if (m_tfY) m.insert(m_tfY, ParamId::TfY);
    if (m_tfRot) m.insert(m_tfRot, ParamId::TfRotation);
    if (m_tfScaleSlider) m.insert(m_tfScaleSlider, ParamId::TfScale);
    if (m_tfScaleEdit) m.insert(m_tfScaleEdit, ParamId::TfScale);
    return m;
}

void ControlsPanel::setScale(float scalePct)
{
    if (!m_tfScaleEdit) return;
    const double m = qMax(0.001, double(scalePct) / 100.0);
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_tfScaleEdit->setText(fmtMult(m));
    QSignalBlocker bl(m_tfScaleSlider);
    m_tfScaleSlider->setValue(multToSlider(m));
    m_updating = wasUpdating;
}

float ControlsPanel::currentScalePct() const
{
    if (!m_tfScaleEdit) return 100.0f;
    bool ok = false;
    const double m = m_tfScaleEdit->text().toDouble(&ok);
    if (ok && m > 0.0) return float(m * 100.0);
    return float(sliderToMult(m_tfScaleSlider->value()) * 100.0);
}

void ControlsPanel::emitTransform()
{
    LayerTransform t;
    t.xPct     = m_curFrameW > 0 ? float(m_tfX->value()) / m_curFrameW : 0.0f;
    t.yPct     = m_curFrameH > 0 ? float(m_tfY->value()) / m_curFrameH : 0.0f;
    t.scalePct = currentScalePct();
    t.rotation = float(m_tfRot->value());
    t.flipH    = m_flipH && m_flipH->isChecked();
    t.flipV    = m_flipV && m_flipV->isChecked();
    emit transformChanged(t);
}

// ── Parameters ───────────────────────────────────────────────

Adjustments ControlsPanel::adjustments() const          { return m_adjust->adjustments(); }
void ControlsPanel::setAdjustments(const Adjustments& a) { m_adjust->setAdjustments(a); }
void ControlsPanel::setSourceImage(const QImage& img)    { m_adjust->setSourceImage(img); }
