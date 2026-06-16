#include "AdjustmentsPanel.h"

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
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget;
    content->setObjectName("controlRoot");
    auto* vlay = new QVBoxLayout(content);
    vlay->setContentsMargins(16, 16, 16, 14);
    vlay->setSpacing(0);

    auto makeSection = [&](const QString& title, auto buildFn) -> CollapsibleSection* {
        auto* rows = new QWidget;
        auto* rl = new QVBoxLayout(rows);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(8);
        buildFn(rl);
        return new CollapsibleSection(title, rows);
    };

    auto addRow = [&](QVBoxLayout* rl, SliderRow*& target, const QString& label,
                      int minV, int maxV, int defV) {
        target = new SliderRow(label, minV, maxV, defV);
        target->onValueChanged = [this](int) { emit adjustmentsChanged(); };
        rl->addWidget(target);
    };

    // ── Tone ─────────────────────────────────────────────────
    auto* toneSection = makeSection("Tone", [&](QVBoxLayout* rl) {
        addRow(rl, m_brightness,  "Brightness",   -100, 100, 0);
        addRow(rl, m_contrast,    "Contrast",     -100, 100, 0);
        addRow(rl, m_gamma,       "Gamma",          10, 300, 100);
        // Levels sub-group
        rl->addWidget(makeParamLabel("Levels"));
        m_levels = new LevelsWidget;
        m_levels->onChanged = [this]() { emit adjustmentsChanged(); };
        rl->addWidget(m_levels);
        addRow(rl, m_saturation,  "Saturation",   -100, 100,   0);
    });

    // ── Detail ───────────────────────────────────────────────
    auto* detailSection = makeSection("Detail", [&](QVBoxLayout* rl) {
        addRow(rl, m_sharpenStrength, "Sharpen strength",  0, 100, 0);
        addRow(rl, m_sharpenRadius,   "Sharpen radius",    1,  10, 1);
        addRow(rl, m_edgeEnhancement, "Edge enhancement",  0, 100, 0);
        addRow(rl, m_blur,            "Blur",              0, 100, 0);
        addRow(rl, m_grain,           "Grain",             0, 100, 0);
    });

    // ── Resolution ───────────────────────────────────────────
    auto* resolutionSection = makeSection("Resolution", [&](QVBoxLayout* rl) {
        addRow(rl, m_size, "Size", 10, 200, 100);
    });

    // ── Creative ─────────────────────────────────────────────
    auto* creativeSection = makeSection("Creative", [&](QVBoxLayout* rl) {
        addRow(rl, m_posterize, "Posterize",  2, 256, 256);
        addRow(rl, m_threshold, "Threshold",  0, 255,   0);
    });

    // Reset button lives outside sections so it's always visible in the scroll area
    auto* btnReset = new QPushButton("Reset adjustments");
    btnReset->setObjectName("exportBtn");
    btnReset->setFixedHeight(42);
    connect(btnReset, &QPushButton::clicked, this, &AdjustmentsPanel::resetRequested);

    vlay->addWidget(toneSection);
    vlay->addSpacing(8);
    vlay->addWidget(detailSection);
    vlay->addSpacing(8);
    vlay->addWidget(resolutionSection);
    vlay->addSpacing(8);
    vlay->addWidget(creativeSection);
    vlay->addSpacing(12);
    vlay->addWidget(btnReset);
    vlay->addStretch();

    scroll->setWidget(content);
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
