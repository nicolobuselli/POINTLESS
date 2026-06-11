#include "AdjustmentsPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>

AdjustmentsPanel::AdjustmentsPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("sidePanel");
    setFixedWidth(300);

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

    auto* rows = new QWidget;
    auto* rl = new QVBoxLayout(rows);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(8);

    auto addRow = [&](SliderRow*& target, const QString& label,
                      int minV, int maxV, int defV) {
        target = new SliderRow(label, minV, maxV, defV);
        target->onValueChanged = [this](int) { emit adjustmentsChanged(); };
        rl->addWidget(target);
    };

    addRow(m_brightness,      "Brightness",       -100, 100,   0);
    addRow(m_contrast,        "Contrast",         -100, 100,   0);
    addRow(m_saturation,      "Saturation",       -100, 100,   0);
    addRow(m_size,            "Size",               10, 200, 100);
    addRow(m_sharpenStrength, "Sharpen strength",    0, 100,   0);
    addRow(m_sharpenRadius,   "Sharpen radius",      1,  10,   1);
    addRow(m_noise,           "Noise",               0, 100,   0);
    addRow(m_denoise,         "Denoise",             0, 100,   0);
    addRow(m_blur,            "Blur",                0, 100,   0);

    auto* btnReset = new QPushButton("Reset adjustments");
    btnReset->setObjectName("exportBtn");
    btnReset->setFixedHeight(42);
    connect(btnReset, &QPushButton::clicked, this, &AdjustmentsPanel::resetRequested);
    rl->addWidget(btnReset);

    auto* section = new CollapsibleSection("Adjustments", rows);
    vlay->addWidget(section);
    vlay->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    // ── Export (pinned bottom) ───────────────────────────────
    auto* exportBox = new QWidget;
    exportBox->setObjectName("exportBox");
    auto* ev = new QVBoxLayout(exportBox);
    ev->setContentsMargins(16, 12, 16, 14);
    ev->setSpacing(2);

    ev->addWidget(makeSectionTitle("Export"));
    ev->addSpacing(6);

    {
        auto* labelsRow = new QHBoxLayout;
        labelsRow->setContentsMargins(0, 0, 0, 0);
        labelsRow->setSpacing(8);
        labelsRow->addWidget(makeParamLabel("Output name"), 2);
        labelsRow->addWidget(makeParamLabel("Type of file"), 1);
        ev->addLayout(labelsRow);

        auto* fieldsRow = new QHBoxLayout;
        fieldsRow->setContentsMargins(0, 0, 0, 0);
        fieldsRow->setSpacing(8);
        m_edtOutputName = new QLineEdit("output");
        m_cmbFormat     = new NoWheelComboBox;
        m_cmbFormat->addItems({ "SVG", "PNG", "JPG" });
        fieldsRow->addWidget(m_edtOutputName, 2);
        fieldsRow->addWidget(m_cmbFormat, 1);
        ev->addLayout(fieldsRow);
        ev->addSpacing(8);

        auto* btnExport = new QPushButton("Export");
        btnExport->setObjectName("exportBtn");
        btnExport->setFixedHeight(40);
        connect(btnExport, &QPushButton::clicked, this, &AdjustmentsPanel::exportRequested);
        ev->addWidget(btnExport);
    }

    outer->addWidget(exportBox);
}

Adjustments AdjustmentsPanel::adjustments() const
{
    Adjustments a;
    a.brightness      = m_brightness->value();
    a.contrast        = m_contrast->value();
    a.saturation      = m_saturation->value();
    a.sizePct         = m_size->value();
    a.sharpenStrength = m_sharpenStrength->value();
    a.sharpenRadius   = m_sharpenRadius->value();
    a.noise           = m_noise->value();
    a.denoise         = m_denoise->value();
    a.blur            = m_blur->value();
    return a;
}

void AdjustmentsPanel::setAdjustments(const Adjustments& a)
{
    m_brightness->setValue(a.brightness);
    m_contrast->setValue(a.contrast);
    m_saturation->setValue(a.saturation);
    m_size->setValue(a.sizePct);
    m_sharpenStrength->setValue(a.sharpenStrength);
    m_sharpenRadius->setValue(a.sharpenRadius);
    m_noise->setValue(a.noise);
    m_denoise->setValue(a.denoise);
    m_blur->setValue(a.blur);
}

QString AdjustmentsPanel::outputFileName() const { return m_edtOutputName->text(); }
QString AdjustmentsPanel::outputFormat()   const { return m_cmbFormat->currentText(); }
