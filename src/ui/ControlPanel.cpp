#include "ControlPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFrame>
#include <QLabel>
#include <QColorDialog>
#include <QFileDialog>
#include <QColor>
#include <QSizePolicy>
#include <QSpacerItem>
#include <algorithm>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ControlPanel::ControlPanel(QWidget* parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);

    buildLayout();
    connectSignals();
    m_initializing = false;
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

HalftoneParams ControlPanel::currentParams() const
{
    HalftoneParams p;

    p.gridSize   = m_spnGridSize->value();
    p.gamma      = static_cast<float>(m_spnGamma->value());
    p.jitter     = static_cast<float>(m_spnJitter->value());
    p.opacity    = static_cast<float>(m_spnOpacity->value());
    p.symbolSize = static_cast<float>(m_spnSymbolSize->value());

    p.shape = static_cast<HalftoneShape>(m_cmbShape->currentIndex());
    p.customSvgPath = m_svgPath;

    p.strokeEnabled = m_chkStroke->isChecked();
    p.strokeWidth   = static_cast<float>(m_spnStrokeWidth->value());
    p.strokeRadius  = static_cast<float>(m_spnStrokeRadius->value());
    p.strokeColor   = m_strokeColor;

    p.fillColor       = m_fillColor;
    p.useImageColors  = m_chkUseImageColors->isChecked();

    p.multiSymbolEnabled = m_chkMultiSymbol->isChecked();
    for (int i = 0; i < 4; ++i) {
        p.symbolSlots[i].shape     = static_cast<HalftoneShape>(m_slots[i].cmbShape->currentIndex());
        p.symbolSlots[i].svgPath   = m_slots[i].svgPath;
        p.symbolSlots[i].threshold = m_slots[i].spnThreshold->value();
    }

    return p;
}

QString ControlPanel::outputFileName() const { return m_edtOutputName->text(); }
QString ControlPanel::outputFormat()   const { return m_cmbFormat->currentText(); }

// ---------------------------------------------------------------------------
// Build layout
// ---------------------------------------------------------------------------

void ControlPanel::buildLayout()
{
    auto* root = new QWidget(this);
    root->setObjectName("controlRoot");
    auto* vlay = new QVBoxLayout(root);
    vlay->setContentsMargins(12, 12, 12, 12);
    vlay->setSpacing(4);

    // ── HEADER: LOGO + TITLE ───────────────────────────────────────────────
    {
        auto* headerRow = new QHBoxLayout;
        headerRow->setSpacing(10);

        auto* logoLabel = new QLabel;
        QPixmap logoPix(":/logo.png");
        logoLabel->setPixmap(logoPix.scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        logoLabel->setFixedSize(36, 36);

        auto* titleLabel = new QLabel("ULTRA TOOL");
        titleLabel->setObjectName("appTitle");

        headerRow->addWidget(logoLabel);
        headerRow->addWidget(titleLabel);
        headerRow->addStretch();
        vlay->addLayout(headerRow);
        vlay->addWidget(buildSeparator());
    }

    // ── LOAD FILE ──────────────────────────────────────────────────────────
    vlay->addWidget(buildSectionHeader("LOAD FILE"));
    m_btnChooseFile = new QPushButton("Choose File");
    m_btnChooseFile->setObjectName("actionButton");
    m_lblFilePath = new QLabel("No file loaded");
    m_lblFilePath->setWordWrap(true);
    m_lblFilePath->setObjectName("subtleLabel");
    vlay->addWidget(m_btnChooseFile);
    vlay->addWidget(m_lblFilePath);
    vlay->addWidget(buildSeparator());

    // ── HALFTONE SHAPE ─────────────────────────────────────────────────────
    vlay->addWidget(buildSectionHeader("HALFTONE SHAPE"));
    m_cmbShape = new QComboBox;
    m_cmbShape->addItems({"Circle", "Square", "Star", "Spark", "Cross (X)", "Plus (+)", "Custom SVG"});
    vlay->addWidget(m_cmbShape);

    {
        m_wgtSvgRow = new QWidget;
        auto* row = new QHBoxLayout(m_wgtSvgRow);
        row->setContentsMargins(0, 0, 0, 0);
        m_btnLoadSvg = new QPushButton("Load SVG…");
        m_lblSvgPath = new QLabel("—");
        m_lblSvgPath->setObjectName("subtleLabel");
        row->addWidget(m_btnLoadSvg);
        row->addWidget(m_lblSvgPath, 1);
        m_wgtSvgRow->setVisible(false);
        vlay->addWidget(m_wgtSvgRow);
    }
    vlay->addWidget(buildSeparator());

    // ── PARAMETERS ─────────────────────────────────────────────────────────
    vlay->addWidget(buildSectionHeader("PARAMETERS"));
    vlay->addWidget(makeIntRow("Grid Size", &m_sldGridSize, &m_spnGridSize, 2, 100, 20));
    vlay->addWidget(makeFloatRow("Gamma",       &m_sldGamma,      &m_spnGamma,      0.1, 5.0, 1.0, 100));
    vlay->addWidget(makeFloatRow("Jitter",      &m_sldJitter,     &m_spnJitter,     0.0, 1.0, 0.0, 100));
    vlay->addWidget(makeFloatRow("Opacity",     &m_sldOpacity,    &m_spnOpacity,    0.0, 1.0, 1.0, 100));
    vlay->addWidget(makeFloatRow("Symbol Size", &m_sldSymbolSize, &m_spnSymbolSize, 0.1, 3.0, 1.0, 100));
    vlay->addWidget(buildSeparator());

    // ── STROKE ─────────────────────────────────────────────────────────────
    vlay->addWidget(buildSectionHeader("STROKE"));
    m_chkStroke = new QCheckBox("Enable Stroke");
    vlay->addWidget(m_chkStroke);
    vlay->addWidget(makeFloatRow("Stroke Width",  &m_sldStrokeWidth,  &m_spnStrokeWidth,  0.1, 10.0, 1.0, 100));
    vlay->addWidget(makeFloatRow("Stroke Radius", &m_sldStrokeRadius, &m_spnStrokeRadius, 0.0, 50.0, 0.0, 10));

    {
        auto* row = new QHBoxLayout;
        m_btnStrokeColor = new QPushButton("Stroke Color");
        m_lblStrokeColor = new QLabel;
        m_lblStrokeColor->setFixedSize(24, 24);
        m_lblStrokeColor->setStyleSheet("background:#000000; border:1px solid #555;");
        row->addWidget(m_btnStrokeColor);
        row->addWidget(m_lblStrokeColor);
        row->addStretch();
        vlay->addLayout(row);
    }
    vlay->addWidget(buildSeparator());

    // ── COLOR ──────────────────────────────────────────────────────────────
    vlay->addWidget(buildSectionHeader("COLOR"));
    {
        auto* row = new QHBoxLayout;
        m_btnFillColor = new QPushButton("Pick Fill Color");
        m_lblFillColor = new QLabel;
        m_lblFillColor->setFixedSize(24, 24);
        m_lblFillColor->setStyleSheet("background:#000000; border:1px solid #555;");
        row->addWidget(m_btnFillColor);
        row->addWidget(m_lblFillColor);
        row->addStretch();
        vlay->addLayout(row);
    }
    m_chkUseImageColors = new QCheckBox("Use Image Colors");
    vlay->addWidget(m_chkUseImageColors);
    vlay->addWidget(buildSeparator());

    // ── MULTI-SYMBOL MODE ──────────────────────────────────────────────────
    vlay->addWidget(buildSectionHeader("MULTI-SYMBOL MODE"));
    m_chkMultiSymbol = new QCheckBox("Enable Multi-Symbol Mode");
    vlay->addWidget(m_chkMultiSymbol);

    m_wgtMultiSymbol = new QWidget;
    auto* msLay = new QVBoxLayout(m_wgtMultiSymbol);
    msLay->setContentsMargins(0, 4, 0, 0);
    msLay->setSpacing(8);

    const QStringList rangeLabels = {"0–63 (dark)", "64–127", "128–191", "192–255 (light)"};
    for (int i = 0; i < 4; ++i) {
        auto& slot = m_slots[i];

        auto* grp = new QGroupBox(rangeLabels[i]);
        auto* gLay = new QVBoxLayout(grp);
        gLay->setSpacing(4);

        slot.cmbShape = new QComboBox;
        slot.cmbShape->addItems({"Circle", "Square", "Star", "Spark", "Cross (X)", "Plus (+)", "Custom SVG"});

        auto* topRow = new QHBoxLayout;
        slot.btnLoad = new QPushButton("Load SVG…");
        slot.lblName = new QLabel("—");
        slot.lblName->setObjectName("subtleLabel");
        topRow->addWidget(slot.btnLoad);
        topRow->addWidget(slot.lblName, 1);

        // Threshold row
        auto* thrRow = new QHBoxLayout;
        auto* thrLbl = new QLabel("Threshold");
        slot.sldThreshold = new QSlider(Qt::Horizontal);
        slot.sldThreshold->setRange(0, 255);
        slot.sldThreshold->setValue(i * 64);
        slot.spnThreshold = new QSpinBox;
        slot.spnThreshold->setRange(0, 255);
        slot.spnThreshold->setValue(i * 64);
        slot.spnThreshold->setFixedWidth(55);
        thrRow->addWidget(thrLbl);
        thrRow->addWidget(slot.sldThreshold, 1);
        thrRow->addWidget(slot.spnThreshold);

        gLay->addWidget(slot.cmbShape);
        gLay->addLayout(topRow);
        gLay->addLayout(thrRow);
        msLay->addWidget(grp);
    }

    m_wgtMultiSymbol->setVisible(false);
    vlay->addWidget(m_wgtMultiSymbol);
    vlay->addWidget(buildSeparator());

    // ── EXPORT ─────────────────────────────────────────────────────────────
    vlay->addWidget(buildSectionHeader("EXPORT"));
    m_edtOutputName = new QLineEdit("output");
    m_edtOutputName->setPlaceholderText("Output file name (no extension)");
    vlay->addWidget(m_edtOutputName);

    m_cmbFormat = new QComboBox;
    m_cmbFormat->addItems({"PNG", "JPG", "SVG"});
    vlay->addWidget(m_cmbFormat);

    m_btnExport = new QPushButton("Export");
    m_btnExport->setObjectName("exportButton");
    vlay->addWidget(m_btnExport);

    vlay->addStretch();
    setWidget(root);
}

// ---------------------------------------------------------------------------
// Connect all signals
// ---------------------------------------------------------------------------

void ControlPanel::connectSignals()
{
    // Choose file
    connect(m_btnChooseFile, &QPushButton::clicked, this, &ControlPanel::fileRequested);

    // Shape combo
    connect(m_cmbShape, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        bool isSvg = (idx == static_cast<int>(HalftoneShape::CustomSVG));
        m_wgtSvgRow->setVisible(isSvg);
        if (!m_initializing) emit paramsChanged();
    });

    // Load SVG button
    connect(m_btnLoadSvg, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Load SVG", "", "SVG files (*.svg)");
        if (!path.isEmpty()) {
            m_svgPath = path;
            m_lblSvgPath->setText(QFileInfo(path).fileName());
            emit paramsChanged();
        }
    });

    // Parameters – float rows
    auto connectFloat = [&](QSlider* sld, QDoubleSpinBox* spn, int scale) {
        connect(sld, &QSlider::valueChanged, this, [=](int v) {
            spn->blockSignals(true);
            spn->setValue(v / double(scale));
            spn->blockSignals(false);
            if (!m_initializing) emit paramsChanged();
        });
        connect(spn, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v) {
            sld->blockSignals(true);
            sld->setValue(int(v * scale));
            sld->blockSignals(false);
            if (!m_initializing) emit paramsChanged();
        });
    };

    connectFloat(m_sldGamma,      m_spnGamma,      100);
    connectFloat(m_sldJitter,     m_spnJitter,     100);
    connectFloat(m_sldOpacity,    m_spnOpacity,    100);
    connectFloat(m_sldSymbolSize, m_spnSymbolSize, 100);
    connectFloat(m_sldStrokeWidth,  m_spnStrokeWidth,  100);
    connectFloat(m_sldStrokeRadius, m_spnStrokeRadius, 10);

    // Grid size (int)
    connect(m_sldGridSize, &QSlider::valueChanged, this, [this](int v) {
        m_spnGridSize->blockSignals(true);
        m_spnGridSize->setValue(v);
        m_spnGridSize->blockSignals(false);
        if (!m_initializing) emit paramsChanged();
    });
    connect(m_spnGridSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        m_sldGridSize->blockSignals(true);
        m_sldGridSize->setValue(v);
        m_sldGridSize->blockSignals(false);
        if (!m_initializing) emit paramsChanged();
    });

    // Stroke checkbox
    connect(m_chkStroke, &QCheckBox::toggled, this, [this]() {
        if (!m_initializing) emit paramsChanged();
    });

    // Stroke color
    connect(m_btnStrokeColor, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(m_strokeColor, this, "Stroke Color",
                                          QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            m_strokeColor = c;
            m_lblStrokeColor->setStyleSheet(
                QString("background:%1; border:1px solid #555;").arg(c.name(QColor::HexArgb)));
            emit paramsChanged();
        }
    });

    // Fill color
    connect(m_btnFillColor, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(m_fillColor, this, "Fill Color",
                                          QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            m_fillColor = c;
            m_lblFillColor->setStyleSheet(
                QString("background:%1; border:1px solid #555;").arg(c.name(QColor::HexArgb)));
            emit paramsChanged();
        }
    });

    connect(m_chkUseImageColors, &QCheckBox::toggled, this, [this]() {
        if (!m_initializing) emit paramsChanged();
    });

    // Multi-symbol toggle
    connect(m_chkMultiSymbol, &QCheckBox::toggled, this, [this](bool on) {
        m_wgtMultiSymbol->setVisible(on);
        if (!m_initializing) emit paramsChanged();
    });

    // Multi-symbol slots
    for (int i = 0; i < 4; ++i) {
        auto& slot = m_slots[i];

        connect(slot.cmbShape, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (!m_initializing) emit paramsChanged();
        });
        connect(slot.sldThreshold, &QSlider::valueChanged, this, [this, i](int v) {
            m_slots[i].spnThreshold->blockSignals(true);
            m_slots[i].spnThreshold->setValue(v);
            m_slots[i].spnThreshold->blockSignals(false);
            if (!m_initializing) emit paramsChanged();
        });
        connect(slot.spnThreshold, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, i](int v) {
            m_slots[i].sldThreshold->blockSignals(true);
            m_slots[i].sldThreshold->setValue(v);
            m_slots[i].sldThreshold->blockSignals(false);
            if (!m_initializing) emit paramsChanged();
        });
        connect(slot.btnLoad, &QPushButton::clicked, this, [this, i]() {
            QString path = QFileDialog::getOpenFileName(this, "Load SVG", "", "SVG files (*.svg)");
            if (!path.isEmpty()) {
                m_slots[i].svgPath = path;
                m_slots[i].lblName->setText(QFileInfo(path).fileName());
                emit paramsChanged();
            }
        });
    }

    // Export
    connect(m_btnExport, &QPushButton::clicked, this, &ControlPanel::exportRequested);
}

// ---------------------------------------------------------------------------
// Section header
// ---------------------------------------------------------------------------

QWidget* ControlPanel::buildSectionHeader(const QString& title)
{
    auto* lbl = new QLabel(title);
    lbl->setObjectName("sectionHeader");
    return lbl;
}

QFrame* ControlPanel::buildSeparator()
{
    auto* f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    f->setObjectName("separator");
    return f;
}

// ---------------------------------------------------------------------------
// Row factory helpers
// ---------------------------------------------------------------------------

QWidget* ControlPanel::makeFloatRow(const QString& label,
                                     QSlider** sliderOut, QDoubleSpinBox** spinOut,
                                     double minVal, double maxVal, double defVal,
                                     int sliderScale)
{
    auto* w    = new QWidget;
    auto* vl   = new QVBoxLayout(w);
    vl->setContentsMargins(0, 2, 0, 2);
    vl->setSpacing(2);

    auto* lbl  = new QLabel(label);
    lbl->setObjectName("paramLabel");

    auto* row  = new QHBoxLayout;
    auto* sld  = new QSlider(Qt::Horizontal);
    sld->setRange(int(minVal * sliderScale), int(maxVal * sliderScale));
    sld->setValue(int(defVal * sliderScale));

    auto* spn  = new QDoubleSpinBox;
    spn->setRange(minVal, maxVal);
    spn->setSingleStep(1.0 / sliderScale);
    spn->setDecimals(2);
    spn->setValue(defVal);
    spn->setFixedWidth(65);

    row->addWidget(sld, 1);
    row->addWidget(spn);

    vl->addWidget(lbl);
    vl->addLayout(row);

    *sliderOut = sld;
    *spinOut   = spn;
    return w;
}

QWidget* ControlPanel::makeIntRow(const QString& label,
                                   QSlider** sliderOut, QSpinBox** spinOut,
                                   int minVal, int maxVal, int defVal)
{
    auto* w   = new QWidget;
    auto* vl  = new QVBoxLayout(w);
    vl->setContentsMargins(0, 2, 0, 2);
    vl->setSpacing(2);

    auto* lbl = new QLabel(label);
    lbl->setObjectName("paramLabel");

    auto* row = new QHBoxLayout;
    auto* sld = new QSlider(Qt::Horizontal);
    sld->setRange(minVal, maxVal);
    sld->setValue(defVal);

    auto* spn = new QSpinBox;
    spn->setRange(minVal, maxVal);
    spn->setValue(defVal);
    spn->setFixedWidth(65);

    row->addWidget(sld, 1);
    row->addWidget(spn);

    vl->addWidget(lbl);
    vl->addLayout(row);

    *sliderOut = sld;
    *spinOut   = spn;
    return w;
}
