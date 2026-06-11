#include "ModePanel.h"
#include "TonalControlsWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>

// ============================================================
//  HalftonePage
// ============================================================

class HalftonePage : public QWidget
{
public:
    std::function<void()> onChanged;

    HalftonePage(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* vl = new QVBoxLayout(this);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);

        // ── Settings ────────────────────────────────────────
        auto* settingsContent = new QWidget;
        {
            auto* sl = new QVBoxLayout(settingsContent);
            sl->setContentsMargins(0, 0, 0, 0);
            sl->setSpacing(10);

            m_dpi = new SliderRow("Input DPI", 18, 300, 72);
            m_dpi->onValueChanged = [this](int) { fire(); };
            sl->addWidget(m_dpi);

            // Shape header: label + "+"
            {
                auto* hdr = new QWidget;
                auto* hl = new QHBoxLayout(hdr);
                hl->setContentsMargins(0, 0, 0, 0);
                hl->setSpacing(6);
                hl->addWidget(makeParamLabel("Shape"), 1);
                m_shapePlus = makeIconButton(":/icons/plus.svg");
                hl->addWidget(m_shapePlus);
                sl->addWidget(hdr);
            }

            m_shapesContainer = new QWidget;
            m_shapesLayout = new QVBoxLayout(m_shapesContainer);
            m_shapesLayout->setContentsMargins(0, 0, 0, 0);
            m_shapesLayout->setSpacing(6);
            sl->addWidget(m_shapesContainer);

            m_thresholdRow = new QWidget;
            {
                auto* tl = new QVBoxLayout(m_thresholdRow);
                tl->setContentsMargins(0, 4, 0, 0);
                tl->setSpacing(4);
                tl->addWidget(makeParamLabel("Threshold"));
                m_sldThreshold = new NoWheelSlider(Qt::Horizontal);
                m_sldThreshold->setRange(0, 255);
                m_sldThreshold->setValue(128);
                tl->addWidget(m_sldThreshold);
            }
            m_thresholdRow->setVisible(false);
            sl->addWidget(m_thresholdRow);

            connect(m_shapePlus, &QPushButton::clicked, this, [this]() {
                if (m_shapeSlots.size() < 4)
                    addShapeSlot(HalftoneShape::Circle, QString(), false);
            });
            connect(m_sldThreshold, &QSlider::valueChanged, this, [this]() { fire(); });
        }
        vl->addWidget(new CollapsibleSection("Settings", settingsContent));

        vl->addSpacing(16);
        vl->addWidget(makeSeparatorLine());
        vl->addSpacing(16);

        // ── Parameters ──────────────────────────────────────
        auto* paramsContent = new QWidget;
        {
            auto* pl = new QVBoxLayout(paramsContent);
            pl->setContentsMargins(0, 0, 0, 0);
            pl->setSpacing(10);

            m_grid   = new SliderRow("Grid",    2, 100,  20);
            m_gamma  = new SliderRow("Gamma",  10, 500, 100);
            m_size   = new SliderRow("Size",   10, 300, 100);
            m_jitter = new SliderRow("Jitter",  0, 100,   0);
            for (SliderRow* r : { m_grid, m_gamma, m_size, m_jitter }) {
                r->onValueChanged = [this](int) { fire(); };
                pl->addWidget(r);
            }

            auto* row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(8);
            m_opacity      = new DragSpinBox(":/icons/opacity.svg",       0, 100, 100, "%");
            m_cornerRadius = new DragSpinBox(":/icons/corner_radius.svg", 0,  50,   0, "");
            m_opacity->onValueChanged      = [this](int) { fire(); };
            m_cornerRadius->onValueChanged = [this](int) { fire(); };
            row->addWidget(m_opacity, 1);
            row->addWidget(m_cornerRadius, 1);
            pl->addLayout(row);
        }
        vl->addWidget(new CollapsibleSection("Parameters", paramsContent));

        vl->addSpacing(16);
        vl->addWidget(makeSeparatorLine());
        vl->addSpacing(16);

        // ── Tonal controls ──────────────────────────────────
        m_tonal = new TonalControlsWidget(TonalSettings{ ToneMode::FixedTones, defaultTones(3) });
        m_tonal->onChanged = [this]() { fire(); };
        vl->addWidget(new CollapsibleSection("Tonal controls", m_tonal));

        addShapeSlot(HalftoneShape::Circle, QString(), true);
    }

    HalftoneSettings settings() const
    {
        HalftoneSettings s;
        s.inputDpi = m_dpi->value();
        s.shapes.clear();
        for (const auto& slot : m_shapeSlots) {
            ShapeEntry e;
            e.shape   = static_cast<HalftoneShape>(slot.combo->currentIndex());
            e.svgPath = slot.svgPath;
            s.shapes.push_back(e);
        }
        s.multiThreshold = m_sldThreshold->value();
        s.gridSize       = m_grid->value();
        s.gamma          = m_gamma->value()  / 100.0f;
        s.symbolSize     = m_size->value()   / 100.0f;
        s.jitter         = m_jitter->value() / 100.0f;
        s.opacity        = m_opacity->value() / 100.0f;
        s.cornerRadius   = float(m_cornerRadius->value());
        s.tonal          = m_tonal->settings();
        return s;
    }

    void setSettings(const HalftoneSettings& s)
    {
        m_updating = true;
        m_dpi->setValue(s.inputDpi);

        clearShapeSlots();
        if (s.shapes.empty()) {
            addShapeSlot(HalftoneShape::Circle, QString(), true);
        } else {
            for (const auto& e : s.shapes)
                addShapeSlot(e.shape, e.svgPath, true);
        }

        m_sldThreshold->blockSignals(true);
        m_sldThreshold->setValue(s.multiThreshold);
        m_sldThreshold->blockSignals(false);

        m_grid->setValue(s.gridSize);
        m_gamma->setValue(qRound(s.gamma * 100));
        m_size->setValue(qRound(s.symbolSize * 100));
        m_jitter->setValue(qRound(s.jitter * 100));
        m_opacity->setValue(qRound(s.opacity * 100));
        m_cornerRadius->setValue(qRound(s.cornerRadius));
        m_tonal->setSettings(s.tonal);
        m_updating = false;
    }

private:
    struct ShapeSlot {
        QWidget*         widget   = nullptr;
        NoWheelComboBox* combo    = nullptr;
        QPushButton*     minusBtn = nullptr;
        QWidget*         svgRow   = nullptr;
        QPushButton*     svgBtn   = nullptr;
        QString          svgPath;
    };

    void fire() { if (!m_updating && onChanged) onChanged(); }

    void clearShapeSlots()
    {
        for (auto& slot : m_shapeSlots) {
            m_shapesLayout->removeWidget(slot.widget);
            slot.widget->deleteLater();
        }
        m_shapeSlots.clear();
    }

    void addShapeSlot(HalftoneShape shape, const QString& svgPath, bool silent)
    {
        if (m_shapeSlots.size() >= 4) return;

        ShapeSlot slot;
        slot.widget = new QWidget;
        auto* outer = new QVBoxLayout(slot.widget);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(4);

        auto* mainRow = new QHBoxLayout;
        mainRow->setContentsMargins(0, 0, 0, 0);
        mainRow->setSpacing(6);

        slot.combo = new NoWheelComboBox;
        slot.combo->addItems({ "Triangle", "Circle", "Square", "Star", "Custom SVG" });
        slot.combo->setCurrentIndex(static_cast<int>(shape));
        slot.minusBtn = makeIconButton(":/icons/minus.svg");
        slot.svgPath  = svgPath;

        mainRow->addWidget(slot.combo, 1);
        mainRow->addWidget(slot.minusBtn);

        slot.svgRow = new QWidget;
        {
            auto* svgLay = new QHBoxLayout(slot.svgRow);
            svgLay->setContentsMargins(0, 0, 30, 0);
            slot.svgBtn = new QPushButton;
            slot.svgBtn->setObjectName("uploadBtn");
            slot.svgBtn->setFixedHeight(34);
            if (svgPath.isEmpty()) {
                slot.svgBtn->setIcon(QIcon(":/icons/upload.svg"));
                slot.svgBtn->setIconSize(QSize(16, 17));
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

        outer->addLayout(mainRow);
        outer->addWidget(slot.svgRow);

        m_shapeSlots.append(slot);
        m_shapesLayout->addWidget(slot.widget);

        for (auto& s : m_shapeSlots) s.minusBtn->setEnabled(m_shapeSlots.size() > 1);

        QWidget*         slotWidget = slot.widget;
        QWidget*         svgRow     = slot.svgRow;
        QPushButton*     svgBtn     = slot.svgBtn;
        NoWheelComboBox* combo      = slot.combo;

        connect(slot.minusBtn, &QPushButton::clicked, this, [this, slotWidget]() {
            removeShapeSlot(slotWidget);
        });
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, svgRow](int idx) {
            svgRow->setVisible(idx == static_cast<int>(HalftoneShape::CustomSVG));
            fire();
        });
        connect(svgBtn, &QPushButton::clicked, this, [this, slotWidget, svgBtn, combo]() {
            QString path = QFileDialog::getOpenFileName(this, "Load SVG", "", "SVG files (*.svg)");
            if (!path.isEmpty()) {
                for (auto& s : m_shapeSlots)
                    if (s.widget == slotWidget) { s.svgPath = path; break; }
                const int ci = static_cast<int>(HalftoneShape::CustomSVG);
                combo->setItemIcon(ci, QIcon(path));
                combo->setItemText(ci, "  " + QFileInfo(path).baseName());
                svgBtn->setIcon(QIcon());
                svgBtn->setText("  Change SVG");
                fire();
            }
        });

        refreshThreshold();
        if (!silent) fire();
    }

    void removeShapeSlot(QWidget* slotWidget)
    {
        if (m_shapeSlots.size() <= 1) return;
        int idx = -1;
        for (int i = 0; i < m_shapeSlots.size(); ++i)
            if (m_shapeSlots[i].widget == slotWidget) { idx = i; break; }
        if (idx < 0) return;
        m_shapesLayout->removeWidget(slotWidget);
        slotWidget->deleteLater();
        m_shapeSlots.removeAt(idx);
        for (auto& s : m_shapeSlots) s.minusBtn->setEnabled(m_shapeSlots.size() > 1);
        refreshThreshold();
        fire();
    }

    void refreshThreshold()
    {
        m_thresholdRow->setVisible(m_shapeSlots.size() > 1);
        if (!m_shapeSlots.isEmpty())
            m_shapeSlots[0].minusBtn->setEnabled(m_shapeSlots.size() > 1);
    }

    SliderRow*           m_dpi             = nullptr;
    QPushButton*         m_shapePlus       = nullptr;
    QWidget*             m_shapesContainer = nullptr;
    QVBoxLayout*         m_shapesLayout    = nullptr;
    QWidget*             m_thresholdRow    = nullptr;
    NoWheelSlider*       m_sldThreshold    = nullptr;
    QVector<ShapeSlot>   m_shapeSlots;

    SliderRow*   m_grid   = nullptr;
    SliderRow*   m_gamma  = nullptr;
    SliderRow*   m_size   = nullptr;
    SliderRow*   m_jitter = nullptr;
    DragSpinBox* m_opacity      = nullptr;
    DragSpinBox* m_cornerRadius = nullptr;

    TonalControlsWidget* m_tonal = nullptr;
    bool m_updating = false;
};

// ============================================================
//  DitherPage
// ============================================================

class DitherPage : public QWidget
{
public:
    std::function<void()> onChanged;

    DitherPage(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* vl = new QVBoxLayout(this);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);

        // ── Settings ────────────────────────────────────────
        auto* settingsContent = new QWidget;
        {
            auto* sl = new QVBoxLayout(settingsContent);
            sl->setContentsMargins(0, 0, 0, 0);
            sl->setSpacing(10);

            sl->addWidget(makeParamLabel("Algorithm"));
            m_algorithm = new NoWheelComboBox;
            m_algorithm->addItems({
                QString::fromUtf8("Floyd–Steinberg"),
                QString::fromUtf8("Jarvis–Judice–Ninke"),
                "Burkes",
                "Atkinson",
                "Bayer",
                "Row modulation",
                "Column modulation",
                "Dispersed modulation",
                "Heavy modulation",
                "Circuit modulation"
            });
            sl->addWidget(m_algorithm);

            m_matrixRow = new QWidget;
            {
                auto* ml = new QVBoxLayout(m_matrixRow);
                ml->setContentsMargins(0, 0, 0, 0);
                ml->setSpacing(4);
                ml->addWidget(makeParamLabel("Bayer matrix"));
                m_matrix = new NoWheelComboBox;
                m_matrix->addItems({ QString::fromUtf8("2×2"), QString::fromUtf8("4×4"),
                                     QString::fromUtf8("8×8"), QString::fromUtf8("16×16") });
                m_matrix->setCurrentIndex(2);
                ml->addWidget(m_matrix);
            }
            m_matrixRow->setVisible(false);
            sl->addWidget(m_matrixRow);

            connect(m_algorithm, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int idx) {
                m_matrixRow->setVisible(idx == int(DitherAlgorithm::Bayer));
                fire();
            });
            connect(m_matrix, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int) { fire(); });
        }
        vl->addWidget(new CollapsibleSection("Settings", settingsContent));

        vl->addSpacing(16);
        vl->addWidget(makeSeparatorLine());
        vl->addSpacing(16);

        // ── Parameters ──────────────────────────────────────
        auto* paramsContent = new QWidget;
        {
            auto* pl = new QVBoxLayout(paramsContent);
            pl->setContentsMargins(0, 0, 0, 0);
            pl->setSpacing(10);

            m_pixelSize = new SliderRow("Pixel size", 1, 16, 2);
            m_strength  = new SliderRow("Strength",   0, 100, 100);
            m_levels    = new SliderRow("Levels (image colors)", 2, 8, 2);
            for (SliderRow* r : { m_pixelSize, m_strength, m_levels }) {
                r->onValueChanged = [this](int) { fire(); };
                pl->addWidget(r);
            }
        }
        vl->addWidget(new CollapsibleSection("Parameters", paramsContent));

        vl->addSpacing(16);
        vl->addWidget(makeSeparatorLine());
        vl->addSpacing(16);

        // ── Tonal controls ──────────────────────────────────
        m_tonal = new TonalControlsWidget(TonalSettings{ ToneMode::FixedTones, defaultTones(1) });
        m_tonal->onChanged = [this]() { fire(); };
        vl->addWidget(new CollapsibleSection("Tonal controls", m_tonal));
    }

    DitherSettings settings() const
    {
        DitherSettings s;
        s.algorithm = static_cast<DitherAlgorithm>(m_algorithm->currentIndex());
        static const int sizes[] = { 2, 4, 8, 16 };
        s.bayerSize = sizes[qBound(0, m_matrix->currentIndex(), 3)];
        s.pixelSize = m_pixelSize->value();
        s.strength  = m_strength->value();
        s.levels    = m_levels->value();
        s.tonal     = m_tonal->settings();
        return s;
    }

    void setSettings(const DitherSettings& s)
    {
        m_updating = true;
        m_algorithm->blockSignals(true);
        m_algorithm->setCurrentIndex(int(s.algorithm));
        m_algorithm->blockSignals(false);
        m_matrixRow->setVisible(s.algorithm == DitherAlgorithm::Bayer);

        int mi = 2;
        if      (s.bayerSize == 2)  mi = 0;
        else if (s.bayerSize == 4)  mi = 1;
        else if (s.bayerSize == 8)  mi = 2;
        else if (s.bayerSize == 16) mi = 3;
        m_matrix->blockSignals(true);
        m_matrix->setCurrentIndex(mi);
        m_matrix->blockSignals(false);

        m_pixelSize->setValue(s.pixelSize);
        m_strength->setValue(s.strength);
        m_levels->setValue(s.levels);
        m_tonal->setSettings(s.tonal);
        m_updating = false;
    }

private:
    void fire() { if (!m_updating && onChanged) onChanged(); }

    NoWheelComboBox* m_algorithm = nullptr;
    QWidget*         m_matrixRow = nullptr;
    NoWheelComboBox* m_matrix    = nullptr;
    SliderRow*       m_pixelSize = nullptr;
    SliderRow*       m_strength  = nullptr;
    SliderRow*       m_levels    = nullptr;
    TonalControlsWidget* m_tonal = nullptr;
    bool m_updating = false;
};

// ============================================================
//  AsciiPage
// ============================================================

class AsciiPage : public QWidget
{
public:
    std::function<void()> onChanged;

    AsciiPage(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* vl = new QVBoxLayout(this);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);

        // ── Settings ────────────────────────────────────────
        auto* settingsContent = new QWidget;
        {
            auto* sl = new QVBoxLayout(settingsContent);
            sl->setContentsMargins(0, 0, 0, 0);
            sl->setSpacing(10);

            sl->addWidget(makeParamLabel("Charset"));
            m_charset = new NoWheelComboBox;
            for (const auto& preset : asciiCharsetPresets())
                m_charset->addItem(preset.name);
            m_charset->addItem("Custom");
            sl->addWidget(m_charset);

            m_customEdit = new QLineEdit;
            m_customEdit->setPlaceholderText("Light → dark characters…");
            m_customEdit->setVisible(false);
            sl->addWidget(m_customEdit);

            m_invert = new QPushButton("Invert");
            m_invert->setCheckable(true);
            m_invert->setFixedHeight(26);
            m_invert->setStyleSheet(
                "QPushButton{background:#3B3B3B;border:1px solid #5D5D5D;border-radius:4px;"
                "color:#B2B2B2;font-size:8pt;padding:0 10px;}"
                "QPushButton:checked{background:#484848;border-color:#828282;color:#E3E3E3;}"
                "QPushButton:hover{border-color:#828282;}");
            sl->addWidget(m_invert);

            connect(m_charset, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int idx) {
                m_customEdit->setVisible(idx >= int(asciiCharsetPresets().size()));
                fire();
            });
            connect(m_customEdit, &QLineEdit::textChanged, this, [this](const QString&) { fire(); });
            connect(m_invert, &QPushButton::toggled, this, [this](bool) { fire(); });
        }
        vl->addWidget(new CollapsibleSection("Settings", settingsContent));

        vl->addSpacing(16);
        vl->addWidget(makeSeparatorLine());
        vl->addSpacing(16);

        // ── Parameters ──────────────────────────────────────
        auto* paramsContent = new QWidget;
        {
            auto* pl = new QVBoxLayout(paramsContent);
            pl->setContentsMargins(0, 0, 0, 0);
            pl->setSpacing(10);

            m_cellSize = new SliderRow("Cell size", 4, 48, 12);
            m_gamma    = new SliderRow("Gamma",    10, 500, 100);
            for (SliderRow* r : { m_cellSize, m_gamma }) {
                r->onValueChanged = [this](int) { fire(); };
                pl->addWidget(r);
            }
        }
        vl->addWidget(new CollapsibleSection("Parameters", paramsContent));

        vl->addSpacing(16);
        vl->addWidget(makeSeparatorLine());
        vl->addSpacing(16);

        // ── Tonal controls ──────────────────────────────────
        m_tonal = new TonalControlsWidget(TonalSettings{ ToneMode::FixedTones, defaultTones(1) });
        m_tonal->onChanged = [this]() { fire(); };
        vl->addWidget(new CollapsibleSection("Tonal controls", m_tonal));
    }

    AsciiSettings settings() const
    {
        AsciiSettings s;
        s.charsetPreset = m_charset->currentIndex();
        s.customCharset = m_customEdit->text();
        s.cellSize      = m_cellSize->value();
        s.gamma         = m_gamma->value() / 100.0f;
        s.invert        = m_invert->isChecked();
        s.tonal         = m_tonal->settings();
        return s;
    }

    void setSettings(const AsciiSettings& s)
    {
        m_updating = true;
        m_charset->blockSignals(true);
        m_charset->setCurrentIndex(qBound(0, s.charsetPreset, m_charset->count() - 1));
        m_charset->blockSignals(false);
        m_customEdit->blockSignals(true);
        m_customEdit->setText(s.customCharset);
        m_customEdit->blockSignals(false);
        m_customEdit->setVisible(s.charsetPreset >= int(asciiCharsetPresets().size()));
        m_cellSize->setValue(s.cellSize);
        m_gamma->setValue(qRound(s.gamma * 100));
        m_invert->blockSignals(true);
        m_invert->setChecked(s.invert);
        m_invert->blockSignals(false);
        m_tonal->setSettings(s.tonal);
        m_updating = false;
    }

private:
    void fire() { if (!m_updating && onChanged) onChanged(); }

    NoWheelComboBox* m_charset    = nullptr;
    QLineEdit*       m_customEdit = nullptr;
    QPushButton*     m_invert     = nullptr;
    SliderRow*       m_cellSize   = nullptr;
    SliderRow*       m_gamma      = nullptr;
    TonalControlsWidget* m_tonal  = nullptr;
    bool m_updating = false;
};

// ============================================================
//  ModePanel
// ============================================================

ModePanel::ModePanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("sidePanel");
    setFixedWidth(340);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── Tabs ─────────────────────────────────────────────────
    {
        auto* tabRow = new QWidget;
        tabRow->setObjectName("tabRow");
        auto* tl = new QHBoxLayout(tabRow);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(0);

        auto makeTab = [&](const QString& text) {
            auto* b = new QPushButton(text);
            b->setObjectName("tabBtn");
            b->setCheckable(true);
            b->setAutoExclusive(true);
            b->setFixedHeight(42);
            b->setCursor(Qt::PointingHandCursor);
            tl->addWidget(b, 1);
            return b;
        };
        m_tabHalftone = makeTab("Halftone");
        m_tabDither   = makeTab("Dither");
        m_tabAscii    = makeTab("Ascii");
        m_tabHalftone->setChecked(true);

        connect(m_tabHalftone, &QPushButton::clicked, this, [this]() { setMode(RenderMode::Halftone, true); });
        connect(m_tabDither,   &QPushButton::clicked, this, [this]() { setMode(RenderMode::Dither,   true); });
        connect(m_tabAscii,    &QPushButton::clicked, this, [this]() { setMode(RenderMode::Ascii,    true); });

        outer->addWidget(tabRow);
    }

    // ── Scrollable page area ─────────────────────────────────
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget;
    content->setObjectName("controlRoot");
    auto* cl = new QVBoxLayout(content);
    cl->setContentsMargins(16, 18, 16, 16);
    cl->setSpacing(0);

    m_halftonePage = new HalftonePage;
    m_ditherPage   = new DitherPage;
    m_asciiPage    = new AsciiPage;
    m_ditherPage->setVisible(false);
    m_asciiPage->setVisible(false);

    m_halftonePage->onChanged = [this]() { if (!m_updating) emit paramsChanged(); };
    m_ditherPage->onChanged   = [this]() { if (!m_updating) emit paramsChanged(); };
    m_asciiPage->onChanged    = [this]() { if (!m_updating) emit paramsChanged(); };

    cl->addWidget(m_halftonePage);
    cl->addWidget(m_ditherPage);
    cl->addWidget(m_asciiPage);
    cl->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    // ── Background (pinned bottom) ───────────────────────────
    outer->addWidget(makeSeparatorLine());
    {
        auto* bgBox = new QWidget;
        bgBox->setObjectName("exportBox");
        auto* bl = new QVBoxLayout(bgBox);
        bl->setContentsMargins(16, 12, 16, 14);
        bl->setSpacing(8);
        bl->addWidget(makeSectionTitle("Background"));

        m_bgSwatch = new FillSwatch(QColor(0xD9, 0xD9, 0xD9), 1.0f, /*showOpacity*/ true);
        m_bgSwatch->onOpacityDragged = [this](float) { if (!m_updating) emit paramsChanged(); };
        m_bgSwatch->onClicked = [this]() {
            const QColor origColor = m_bgSwatch->color();
            const float  origOp    = m_bgSwatch->opacity();

            auto* dlg = new ColorPickerDialog(origColor, origOp, /*showOpacity*/ true, this);
            dlg->moveNextTo(m_bgSwatch);
            dlg->onColorChanged = [this](QColor c, float a) {
                m_bgSwatch->setColor(c);
                m_bgSwatch->setOpacity(a);
                if (!m_updating) emit paramsChanged();
            };
            if (dlg->exec() == QDialog::Accepted) {
                m_bgSwatch->setColor(dlg->selectedColor());
                m_bgSwatch->setOpacity(dlg->selectedOpacity());
            } else {
                m_bgSwatch->setColor(origColor);
                m_bgSwatch->setOpacity(origOp);
            }
            emit paramsChanged();
            dlg->deleteLater();
        };
        bl->addWidget(m_bgSwatch);

        outer->addWidget(bgBox);
    }
}

HalftoneSettings ModePanel::halftoneSettings() const { return m_halftonePage->settings(); }
DitherSettings   ModePanel::ditherSettings()   const { return m_ditherPage->settings(); }
AsciiSettings    ModePanel::asciiSettings()    const { return m_asciiPage->settings(); }
QColor           ModePanel::background()        const { return m_bgSwatch->color(); }
float            ModePanel::backgroundOpacity() const { return m_bgSwatch->opacity(); }

void ModePanel::setMode(RenderMode m, bool notify)
{
    m_mode = m;
    m_halftonePage->setVisible(m == RenderMode::Halftone);
    m_ditherPage->setVisible(m == RenderMode::Dither);
    m_asciiPage->setVisible(m == RenderMode::Ascii);
    m_tabHalftone->setChecked(m == RenderMode::Halftone);
    m_tabDither->setChecked(m == RenderMode::Dither);
    m_tabAscii->setChecked(m == RenderMode::Ascii);
    if (notify && !m_updating) emit paramsChanged();
}

void ModePanel::setAll(const SessionParams& p)
{
    m_updating = true;
    m_halftonePage->setSettings(p.halftone);
    m_ditherPage->setSettings(p.dither);
    m_asciiPage->setSettings(p.ascii);
    m_bgSwatch->setColor(p.background);
    m_bgSwatch->setOpacity(p.backgroundOpacity);
    setMode(p.mode, false);
    m_updating = false;
}
