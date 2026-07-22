#include "AdjustmentsPanel.h"
#include "Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
#include <QLabel>
#include <QIcon>
#include <QStyle>
#include <QPainter>
#include <QSvgRenderer>
#include <QPixmap>
#include <array>
#include <cmath>

// Invert/Localize on/off indicator: the same clover silhouette as the app
// logo and the #iconBtn hover shape (hover_shape.svg), not a rounded square.
// Idle = a thin ring in the clover outline (@boxStroke); checked = the
// clover filled solid yellow with a dark checkmark on top — the "yellow",
// not blue (@boxStrokeActive) like the old rounded-square indicator.
class CheckSquare : public QWidget
{
public:
    explicit CheckSquare(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedSize(Ui::px(22), Ui::px(22));
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setChecked(bool on)
    {
        if (m_checked == on) return;
        m_checked = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        static QSvgRenderer cloverRenderer(QString(":/icons/hover_shape.svg"));
        static QSvgRenderer checkRenderer(QString(":/icons/check.svg"));

        // Renders `renderer` into an alpha mask the size of this widget, then
        // flood-fills it with `color` via DestinationIn — recolors a flat
        // baked-color SVG without needing a separate asset per tint.
        auto tintedMask = [this](QSvgRenderer& renderer, const QColor& color, const QRectF& area) {
            QPixmap mask(size());
            mask.fill(Qt::transparent);
            { QPainter mp(&mask); renderer.render(&mp, area); }
            QPixmap tinted(size());
            tinted.fill(Qt::transparent);
            QPainter tp(&tinted);
            tp.fillRect(tinted.rect(), color);
            tp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            tp.drawPixmap(0, 0, mask);
            return tinted;
        };

        QPainter p(this);
        const QRectF full = rect();

        if (m_checked) {
            p.drawPixmap(0, 0, tintedMask(cloverRenderer, QColor("#D2FC51"), full));
            p.drawPixmap(0, 0, tintedMask(checkRenderer, QColor("#1E1E1E"),
                                          full.adjusted(5, 5, -5, -5)));
        } else {
            // Ring = outer clover minus a smaller inner clover (DestinationOut),
            // same "stroke, not fill" language as every other idle box.
            const qreal strokeW = 1.5;
            QPixmap outer(size());
            outer.fill(Qt::transparent);
            { QPainter op(&outer); cloverRenderer.render(&op, full); }
            QPixmap inner(size());
            inner.fill(Qt::transparent);
            { QPainter ip(&inner);
              cloverRenderer.render(&ip, full.adjusted(strokeW, strokeW, -strokeW, -strokeW)); }
            { QPainter op2(&outer);
              op2.setCompositionMode(QPainter::CompositionMode_DestinationOut);
              op2.drawPixmap(0, 0, inner); }

            QPixmap ring(size());
            ring.fill(Qt::transparent);
            QPainter rp(&ring);
            rp.fillRect(ring.rect(), QColor("#3D3D3D"));   // @boxStroke
            rp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            rp.drawPixmap(0, 0, outer);
            rp.end();
            p.drawPixmap(0, 0, ring);
        }
    }

private:
    bool m_checked = false;
};

namespace {
// Invert/Localize box: label on the left, a CheckSquare indicator on the
// right — the box itself stays the standard idle chrome, only the square
// shows on/off state.
QPushButton* makeCheckRow(const QString& text, CheckSquare*& squareOut)
{
    auto* btn = new QPushButton;
    btn->setObjectName("checkRow");
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(Ui::px(Ui::kBoxH));

    auto* hl = new QHBoxLayout(btn);
    hl->setContentsMargins(Ui::px(14), 0, Ui::px(12), 0);
    hl->setSpacing(Ui::px(8));

    auto* label = new QLabel(text);
    label->setObjectName("checkRowLabel");
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    hl->addWidget(label, 1);

    auto* square = new CheckSquare;
    hl->addWidget(square, 0, Qt::AlignVCenter);

    QObject::connect(btn, &QPushButton::toggled, btn, [square](bool on) { square->setChecked(on); });

    squareOut = square;
    return btn;
}
} // namespace

AdjustmentsPanel::AdjustmentsPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("sidePanel");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── Scrollable adjustments ───────────────────────────────
    // The scrollbar auto-hides (only shows while interacting) — see
    // AutoHideScroll applied by ControlsPanel.
    auto* scroll = new QScrollArea;
    scroll->setObjectName("paramsScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    m_scroll = scroll;

    auto* content = new QWidget;
    content->setObjectName("controlRoot");
    auto* vlay = new QVBoxLayout(content);
    m_vlay = vlay;
    // Left edge aligns with the section titles (40px); the right keeps a 70px
    // gutter free for the +/−/favourite icon column; standard title→first gap
    // and row rhythm (Theme.h). ControlsPanel prepends the Image Adjustments
    // (Position/Rotation/Scale) rows above Brightness at index 0, so this is
    // the "title band → first control" gap for the whole merged section, and
    // everything below scrolls together as one list.
    vlay->setContentsMargins(Ui::px(Ui::kColLeft), Ui::px(Ui::kGapTitleToFirst),
                             Ui::px(Ui::kColRight), Ui::px(12));
    vlay->setSpacing(Ui::px(Ui::kGapRows));

    auto addRow = [&](SliderRow*& target, const QString& label,
                      int minV, int maxV, int defV) {
        target = new SliderRow(label, minV, maxV, defV);
        target->onValueChanged = [this](int) { emit adjustmentsChanged(); };
        vlay->addWidget(target);
    };

    // Visible parameters (the trimmed set): Brightness · Contrast · Gamma ·
    // Levels · Blur · Grain · Posterize — a flat list, no collapsible groups.
    addRow(m_brightness, "Brightness", -100, 100,   0);
    addRow(m_contrast,   "Contrast",   -100, 100,   0);
    addRow(m_gamma,      "Gamma",        10, 300, 100);

    m_levels = new LevelsWidget;
    m_levels->onChanged = [this]() { emit adjustmentsChanged(); };
    vlay->addWidget(makeLabeledGroup("Levels", m_levels));

    addRow(m_blur,      "Blur",      0, 100,   0);
    addRow(m_grain,     "Grain",     0, 100,   0);
    addRow(m_posterize, "Posterize", 2, 256, 256);

    // Invert + Localize: share one row, each a checkRow box with its own
    // checkmark-square indicator (see makeCheckRow above). Sits at the foot
    // of the list, right before the reset button.
    auto* checkRow = new QHBoxLayout;
    checkRow->setContentsMargins(0, 0, 0, 0);
    checkRow->setSpacing(Ui::px(Ui::kGapTwinBoxes));

    m_invert = makeCheckRow("Invert", m_invertSquare);
    connect(m_invert, &QPushButton::toggled, this,
            [this](bool) { emit adjustmentsChanged(); });
    checkRow->addWidget(m_invert, 1);

    // Whole-layer localization: masks the layer's effect to an on-canvas
    // circle (position/radius/falloff) instead of a per-parameter dot next
    // to every slider. One button, one point — simpler interaction; MainWindow
    // maps this to the active layer kind's mask LocParam.
    m_localize = makeCheckRow("Localize", m_localizeSquare);
    connect(m_localize, &QPushButton::clicked, this,
            [this]() { emit localizeToggleRequested(); });
    checkRow->addWidget(m_localize, 1);

    vlay->addLayout(checkRow);

    // Removed-from-UI parameters: kept alive (hidden, default values) so the
    // Adjustments struct round-trips unchanged through collect/apply/undo.
    auto* hidden = new QWidget(content);
    hidden->setVisible(false);
    auto* hl = new QVBoxLayout(hidden);
    hl->setContentsMargins(0, 0, 0, 0);
    auto addHidden = [&](SliderRow*& target, int minV, int maxV, int defV) {
        target = new SliderRow(QString(), minV, maxV, defV);
        hl->addWidget(target);
    };
    addHidden(m_saturation,      -100, 100,   0);
    addHidden(m_sharpenStrength,    0, 100,   0);
    addHidden(m_sharpenRadius,      1,  10,   1);
    addHidden(m_edgeEnhancement,    0, 100,   0);
    addHidden(m_size,              10, 200, 100);
    addHidden(m_threshold,          0, 255,   0);
    vlay->addWidget(hidden);

    // Reset button: always visible at the foot of the scroll area.
    auto* btnReset = new QPushButton("reset adjustments");
    btnReset->setObjectName("resetBtn");
    btnReset->setFixedHeight(Ui::px(Ui::kBoxH));
    btnReset->setCursor(Qt::PointingHandCursor);
    connect(btnReset, &QPushButton::clicked, this, &AdjustmentsPanel::resetRequested);

    vlay->addWidget(btnReset);
    vlay->addStretch();

    scroll->setWidget(content);
    // Floating scrollbar: reserves no width, so the controls keep a constant
    // width whether or not the list overflows when the pane is resized.
    installOverlayScrollbar(scroll);
    outer->addWidget(scroll, 1);
}

Adjustments AdjustmentsPanel::adjustments() const
{
    Adjustments a;
    a.brightness      = m_brightness->value();
    a.contrast        = m_contrast->value();
    a.gamma           = m_gamma->value();
    a.levelsBlack     = m_levels->blackPoint();
    a.levelsMid       = m_levels->midPoint();
    a.levelsWhite     = m_levels->whitePoint();
    a.saturation      = m_saturation->value();
    a.sizePct         = m_size->value();
    a.sharpenStrength = m_sharpenStrength->value();
    a.sharpenRadius   = m_sharpenRadius->value();
    a.edgeEnhancement = m_edgeEnhancement->value();
    a.invert          = m_invert->isChecked();
    a.blur            = m_blur->value();
    a.grain           = m_grain->value();
    a.posterize       = m_posterize->value();
    a.threshold       = m_threshold->value();
    return a;
}

void AdjustmentsPanel::setAdjustments(const Adjustments& a)
{
    m_brightness->setValue(a.brightness);
    m_contrast->setValue(a.contrast);
    m_gamma->setValue(a.gamma);
    m_levels->setValues(a.levelsBlack, a.levelsMid, a.levelsWhite);
    m_saturation->setValue(a.saturation);
    m_size->setValue(a.sizePct);
    m_sharpenStrength->setValue(a.sharpenStrength);
    m_sharpenRadius->setValue(a.sharpenRadius);
    m_edgeEnhancement->setValue(a.edgeEnhancement);
    m_invert->blockSignals(true);
    m_invert->setChecked(a.invert);
    m_invert->blockSignals(false);
    m_invertSquare->setChecked(a.invert);
    m_blur->setValue(a.blur);
    m_grain->setValue(a.grain);
    m_posterize->setValue(a.posterize);
    m_threshold->setValue(a.threshold);
}

void AdjustmentsPanel::prependWidget(QWidget* w)
{
    m_vlay->insertWidget(0, w);
}

void AdjustmentsPanel::setLocalizeChecked(bool on)
{
    m_localize->blockSignals(true);
    m_localize->setChecked(on);
    m_localize->blockSignals(false);
    m_localizeSquare->setChecked(on);
}

void AdjustmentsPanel::scrollToTop()
{
    if (m_scroll) m_scroll->verticalScrollBar()->setValue(0);
}

void AdjustmentsPanel::setSourceImage(const QImage& img)
{
    std::array<int,256> h{};
    if (!img.isNull()) {
        // Downsample large images so the loop stays under ~1ms.
        QImage work = img;
        if (work.width() > 512 || work.height() > 512)
            work = img.scaled(512, 512, Qt::KeepAspectRatio, Qt::FastTransformation);
        work = work.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < work.height(); ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(work.constScanLine(y));
            for (int x = 0; x < work.width(); ++x) {
                const QRgb p = line[x];
                const int lum = qRound(0.299f * qRed(p)
                                     + 0.587f * qGreen(p)
                                     + 0.114f * qBlue(p));
                ++h[qBound(0, lum, 255)];
            }
        }
    }
    m_levels->setHistogram(h);
}

void AdjustmentsPanel::setAnimatedParams(const QSet<ParamId>& ids)
{
    m_brightness->setAnimated(ids.contains(ParamId::AdjBrightness));
    m_contrast->setAnimated(ids.contains(ParamId::AdjContrast));
    m_gamma->setAnimated(ids.contains(ParamId::AdjGamma));
    m_blur->setAnimated(ids.contains(ParamId::AdjBlur));
    m_grain->setAnimated(ids.contains(ParamId::AdjGrain));
    m_posterize->setAnimated(ids.contains(ParamId::AdjPosterize));
}

QHash<QWidget*, ParamId> AdjustmentsPanel::paramWidgets() const
{
    return {
        { m_brightness, ParamId::AdjBrightness },
        { m_contrast,   ParamId::AdjContrast },
        { m_gamma,      ParamId::AdjGamma },
        { m_blur,       ParamId::AdjBlur },
        { m_grain,      ParamId::AdjGrain },
        { m_posterize,  ParamId::AdjPosterize },
    };
}
