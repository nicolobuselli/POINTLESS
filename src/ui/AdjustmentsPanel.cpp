#include "AdjustmentsPanel.h"
#include "UiScale.h"

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
    // gutter free for the +/−/favourite icon column (design spec).
    vlay->setContentsMargins(Ui::px(40), Ui::px(18), Ui::px(70), Ui::px(12));
    vlay->setSpacing(Ui::px(10));

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

    vlay->addWidget(makeParamLabel("Levels"));
    m_levels = new LevelsWidget;
    m_levels->onChanged = [this]() { emit adjustmentsChanged(); };
    vlay->addWidget(m_levels);

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
    btnReset->setFixedHeight(Ui::px(44));
    btnReset->setCursor(Qt::PointingHandCursor);
    connect(btnReset, &QPushButton::clicked, this, &AdjustmentsPanel::resetRequested);

    vlay->addSpacing(2);
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
