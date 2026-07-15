#include "AdjustmentsPanel.h"
#include "Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <array>
#include <cmath>

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

    auto* content = new QWidget;
    content->setObjectName("controlRoot");
    auto* vlay = new QVBoxLayout(content);
    // Left edge aligns with the section titles (40px); the right keeps a 70px
    // gutter free for the +/−/favourite icon column; standard title→first gap
    // and row rhythm (Theme.h).
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

    // Invert: checkable box, same visual language as the DragSpinBox boxes.
    m_invert = new QPushButton("Invert");
    m_invert->setCheckable(true);
    m_invert->setCursor(Qt::PointingHandCursor);
    m_invert->setFixedHeight(Ui::px(Ui::kBoxH));
    m_invert->setStyleSheet(QString(
        "QPushButton{background:%1;border:1px solid %2;border-radius:%3px;"
        "color:%4;font-size:%5px;font-weight:500;}"
        "QPushButton:hover{border-color:%6;}"
        "QPushButton:checked{background:%7;border-color:%6;color:%8;}")
        .arg(Ui::kColBoxBg).arg(Ui::kColBoxBorder).arg(Ui::px(Ui::kBoxRadius))
        .arg(Ui::kColLabel).arg(Ui::px(Ui::kBoxFontPx))
        .arg(Ui::kColBoxHover).arg(Ui::kColBoxChecked).arg(Ui::kColText));
    connect(m_invert, &QPushButton::toggled, this,
            [this](bool) { emit adjustmentsChanged(); });
    vlay->addWidget(m_invert);

    addRow(m_blur,      "Blur",      0, 100,   0);
    addRow(m_grain,     "Grain",     0, 100,   0);
    addRow(m_posterize, "Posterize", 2, 256, 256);

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
    m_blur->setValue(a.blur);
    m_grain->setValue(a.grain);
    m_posterize->setValue(a.posterize);
    m_threshold->setValue(a.threshold);
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
