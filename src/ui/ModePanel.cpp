#include "ModePanel.h"
#include "TonalControlsWidget.h"
#include "UiScale.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QStackedWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QStandardItemModel>

// ============================================================
//  Shared section helpers
// ============================================================

namespace {

// Full-width 1px divider used to bracket section titles (matches the rest
// of the interface).
QFrame* bandLine()
{
    auto* f = new QFrame;
    f->setObjectName("bandLine");
    f->setFixedHeight(1);
    return f;
}

struct BlendItem { BlendMode mode; const char* name; bool groupStart; };
const BlendItem kBlend[] = {
    { BlendMode::Normal, "Normal", false }, { BlendMode::Dissolve, "Dissolve", false },
    { BlendMode::Darken, "Darken", true }, { BlendMode::Multiply, "Multiply", false },
    { BlendMode::ColorBurn, "Color Burn", false }, { BlendMode::LinearBurn, "Linear Burn", false },
    { BlendMode::DarkerColor, "Darker Color", false },
    { BlendMode::Lighten, "Lighten", true }, { BlendMode::Screen, "Screen", false },
    { BlendMode::ColorDodge, "Color Dodge", false }, { BlendMode::LinearDodge, "Linear Dodge (Add)", false },
    { BlendMode::LighterColor, "Lighter Color", false },
    { BlendMode::Overlay, "Overlay", true }, { BlendMode::SoftLight, "Soft Light", false },
    { BlendMode::HardLight, "Hard Light", false }, { BlendMode::VividLight, "Vivid Light", false },
    { BlendMode::LinearLight, "Linear Light", false }, { BlendMode::PinLight, "Pin Light", false },
    { BlendMode::HardMix, "Hard Mix", false },
    { BlendMode::Difference, "Difference", true }, { BlendMode::Exclusion, "Exclusion", false },
    { BlendMode::Subtract, "Subtract", false }, { BlendMode::Divide, "Divide", false },
    { BlendMode::Hue, "Hue", true }, { BlendMode::Saturation, "Saturation", false },
    { BlendMode::Color, "Color", false }, { BlendMode::Luminosity, "Luminosity", false },
};

// A section with a bold title bracketed by lines, an optional +/− collapse
// toggle in the right gutter, and a content area. Fill the section via body().
class PanelSection : public QWidget
{
public:
    std::function<void(bool)> onToggled;

    PanelSection(const QString& title, bool collapsible, bool startOpen,
                 QWidget* parent = nullptr)
        : QWidget(parent), m_collapsible(collapsible)
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        root->addWidget(bandLine());

        auto* titleRow = new QWidget;
        auto* tl = new QHBoxLayout(titleRow);
        tl->setContentsMargins(Ui::px(40), Ui::px(12), Ui::px(24), Ui::px(12));
        tl->setSpacing(0);
        tl->addWidget(makeSectionTitle(title), 1);

        if (collapsible) {
            m_toggle = new QPushButton;
            m_toggle->setObjectName("iconBtn");
            m_toggle->setCursor(Qt::PointingHandCursor);
            m_toggle->setFixedSize(Ui::px(26), Ui::px(26));
            m_toggle->setIconSize(QSize(Ui::px(16), Ui::px(16)));
            connect(m_toggle, &QPushButton::clicked, this, [this]() { setOpen(!m_open); });
            tl->addWidget(m_toggle);
        }
        root->addWidget(titleRow);

        m_content = new QWidget;
        m_body = new QVBoxLayout(m_content);
        m_body->setContentsMargins(Ui::px(40), Ui::px(2), Ui::px(70), Ui::px(14));
        m_body->setSpacing(Ui::px(10));
        root->addWidget(m_content);

        setOpen(startOpen);
    }

    QVBoxLayout* body() const { return m_body; }

    void setOpen(bool open)
    {
        m_open = open;
        m_content->setVisible(open);
        if (m_toggle)
            m_toggle->setIcon(QIcon(open ? ":/icons/minus.svg" : ":/icons/plus.svg"));
        if (onToggled) onToggled(open);
    }
    bool isOpen() const { return m_open; }

private:
    bool         m_collapsible;
    bool         m_open = true;
    QPushButton* m_toggle  = nullptr;
    QWidget*     m_content = nullptr;
    QVBoxLayout* m_body    = nullptr;
};

} // namespace

// ============================================================
//  HalftonePage
// ============================================================

class HalftonePage : public QWidget
{
public:
    std::function<void()> onChanged;

    std::function<void()> onBlendChanged;

    HalftonePage(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* vl = new QVBoxLayout(this);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);

        // ── Shape (single dropdown; DPI fixed at high quality) ──
        auto* shape = new PanelSection("Shape", /*collapsible*/ false, true);
        {
            m_shapeCombo = new NoWheelComboBox;
            m_shapeCombo->addItems({ "Triangle", "Circle", "Square", "Star", "Custom SVG" });
            m_shapeCombo->setCurrentIndex(int(HalftoneShape::Square));
            shape->body()->addWidget(m_shapeCombo);

            m_svgBtn = new QPushButton("  Upload SVG");
            m_svgBtn->setObjectName("uploadBtn");
            m_svgBtn->setFixedHeight(Ui::px(40));
            m_svgBtn->setIcon(QIcon(":/icons/upload.svg"));
            m_svgBtn->setIconSize(QSize(Ui::px(16), Ui::px(16)));
            m_svgBtn->setVisible(false);
            shape->body()->addWidget(m_svgBtn);

            connect(m_shapeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int idx) {
                m_svgBtn->setVisible(idx == int(HalftoneShape::CustomSVG));
                fire();
            });
            connect(m_svgBtn, &QPushButton::clicked, this, [this]() {
                const QString p = QFileDialog::getOpenFileName(this, "Load SVG", "", "SVG files (*.svg)");
                if (!p.isEmpty()) { m_svgPath = p; m_svgBtn->setText("  " + QFileInfo(p).baseName()); fire(); }
            });
        }
        vl->addWidget(shape);

        // ── Settings ────────────────────────────────────────
        auto* settings = new PanelSection("Settings", /*collapsible*/ false, true);
        {
            auto* sl = settings->body();

            sl->addWidget(makeParamLabel("Grid"));
            m_gridType = new NoWheelComboBox;
            m_gridType->addItems({ "Square", "Hexagonal", "Radial", "Line", "Circles" });
            sl->addWidget(m_gridType);

            m_spacing      = new SliderRow("Spacing",       2, 200,  20);
            m_rotation     = new SliderRow("Rotation",      0, 360,   0);
            m_gamma        = new SliderRow("Gamma",        10, 500, 100);
            m_diameter     = new SliderRow("Diameter",     10, 300, 100);
            m_stretch      = new SliderRow("Stretch",      10, 400, 100);
            m_stretchAngle = new SliderRow("Stretch angle", 0, 360,   0);
            m_jitter       = new SliderRow("Jitter",        0, 100,   0);
            for (SliderRow* r : { m_spacing, m_rotation, m_gamma, m_diameter,
                                  m_stretch, m_stretchAngle, m_jitter }) {
                r->onValueChanged = [this](int) { fire(); };
                sl->addWidget(r);
            }

            // Fusion (blend mode of the active layer) — moved here.
            sl->addWidget(makeParamLabel("Fusion"));
            m_fusion = new NoWheelComboBox;
            for (const auto& e : kBlend) {
                if (e.groupStart && m_fusion->count() > 0)
                    m_fusion->insertSeparator(m_fusion->count());
                m_fusion->addItem(QString::fromUtf8(e.name), int(e.mode));
            }
            sl->addWidget(m_fusion);
            connect(m_fusion, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int) { if (!m_updating && onBlendChanged) onBlendChanged(); });

            // Opacity + Corner radius (two icon boxes side by side).
            auto* labels = new QHBoxLayout;
            labels->setContentsMargins(0, 0, 0, 0);
            labels->setSpacing(Ui::px(12));
            labels->addWidget(makeParamLabel("Opacity"), 1);
            labels->addWidget(makeParamLabel("Corner radius"), 1);
            sl->addLayout(labels);

            auto* row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(Ui::px(12));
            m_opacity      = new DragSpinBox(":/icons/opacity.svg",       0, 100, 100, "%");
            m_cornerRadius = new DragSpinBox(":/icons/corner_radius.svg", 0,  50,   0, "");
            m_opacity->onValueChanged      = [this](int) { fire(); };
            m_cornerRadius->onValueChanged = [this](int) { fire(); };
            row->addWidget(m_opacity, 1);
            row->addWidget(m_cornerRadius, 1);
            sl->addLayout(row);

            connect(m_gridType, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int) { fire(); });
        }
        vl->addWidget(settings);
    }

    BlendMode blend() const
    {
        const QVariant v = m_fusion->currentData();
        return v.isValid() ? BlendMode(v.toInt()) : BlendMode::Normal;
    }
    void setBlend(BlendMode m)
    {
        for (int i = 0; i < m_fusion->count(); ++i) {
            const QVariant v = m_fusion->itemData(i);
            if (v.isValid() && v.toInt() == int(m)) { m_fusion->setCurrentIndex(i); break; }
        }
    }

    HalftoneSettings settings() const
    {
        HalftoneSettings s;
        s.inputDpi = 300;                       // fixed high quality (no DPI control)
        s.shapes.clear();
        ShapeEntry e;
        e.shape   = static_cast<HalftoneShape>(m_shapeCombo->currentIndex());
        e.svgPath = m_svgPath;
        s.shapes.push_back(e);
        s.multiThreshold = 128;

        s.grid.type          = static_cast<GridType>(m_gridType->currentIndex());
        s.grid.spacing       = float(m_spacing->value());
        s.grid.rotation      = float(m_rotation->value());
        s.grid.diameter      = m_diameter->value() / 100.0f;
        s.grid.stretchFactor = m_stretch->value()  / 100.0f;
        s.grid.stretchAngle  = float(m_stretchAngle->value());

        s.gamma        = m_gamma->value()  / 100.0f;
        s.jitter       = m_jitter->value() / 100.0f;
        s.opacity      = m_opacity->value() / 100.0f;
        s.cornerRadius = float(m_cornerRadius->value());
        return s;
    }

    void setSettings(const HalftoneSettings& s)
    {
        m_updating = true;
        const ShapeEntry e = s.shapes.empty() ? ShapeEntry{} : s.shapes.front();
        m_svgPath = e.svgPath;
        m_shapeCombo->blockSignals(true);
        m_shapeCombo->setCurrentIndex(int(e.shape));
        m_shapeCombo->blockSignals(false);
        m_svgBtn->setVisible(e.shape == HalftoneShape::CustomSVG);
        m_svgBtn->setText(e.svgPath.isEmpty() ? "  Upload SVG"
                                              : "  " + QFileInfo(e.svgPath).baseName());

        m_gridType->blockSignals(true);
        m_gridType->setCurrentIndex(int(s.grid.type));
        m_gridType->blockSignals(false);
        m_spacing->setValue(qRound(s.grid.spacing));
        m_rotation->setValue(qRound(s.grid.rotation));
        m_diameter->setValue(qRound(s.grid.diameter * 100));
        m_stretch->setValue(qRound(s.grid.stretchFactor * 100));
        m_stretchAngle->setValue(qRound(s.grid.stretchAngle));
        m_gamma->setValue(qRound(s.gamma * 100));
        m_jitter->setValue(qRound(s.jitter * 100));
        m_opacity->setValue(qRound(s.opacity * 100));
        m_cornerRadius->setValue(qRound(s.cornerRadius));
        m_updating = false;
    }

private:
    void fire() { if (!m_updating && onChanged) onChanged(); }

    NoWheelComboBox* m_shapeCombo   = nullptr;
    QPushButton*     m_svgBtn       = nullptr;
    QString          m_svgPath;

    NoWheelComboBox* m_gridType     = nullptr;
    SliderRow*       m_spacing      = nullptr;
    SliderRow*       m_rotation     = nullptr;
    SliderRow*       m_gamma        = nullptr;
    SliderRow*       m_diameter     = nullptr;
    SliderRow*       m_stretch      = nullptr;
    SliderRow*       m_stretchAngle = nullptr;
    SliderRow*       m_jitter       = nullptr;
    NoWheelComboBox* m_fusion       = nullptr;
    DragSpinBox*     m_opacity      = nullptr;
    DragSpinBox*     m_cornerRadius = nullptr;

    bool m_updating = false;
};

// ============================================================
//  DitherPage
// ============================================================

namespace {

struct AlgoEntry {
    DitherAlgorithm algo;
    const char*     label;
    const char*     description;
};

// Groups and items in display order.
// A null label / description marks a category header (disabled item).
const AlgoEntry kAlgoEntries[] = {
    { DitherAlgorithm::FloydSteinberg,      nullptr,               "Error Diffusion"  },  // header
    { DitherAlgorithm::FloydSteinberg,      "Floyd\xe2\x80\x93Steinberg",
        "Classic 4-tap kernel. Balanced tonal accuracy with characteristic worm-pattern artifacts." },
    { DitherAlgorithm::FalseFloydSteinberg, "False Floyd\xe2\x80\x93Steinberg",
        "Lightweight 3-tap FS approximation. Faster, but produces more directional banding." },
    { DitherAlgorithm::Atkinson,            "Atkinson",
        "Diffuses 75\xe2\x80\x89% of error. Retains bright areas; the iconic Mac / early-desktop aesthetic." },
    { DitherAlgorithm::Burkes,              "Burkes",
        "Simplified JJN with a 7-tap 2-row kernel. Reduced directional bias, smooth mid-tones." },
    { DitherAlgorithm::Sierra,              "Sierra",
        "10-tap 3-row kernel. Refined gradients with less worm artifact than Floyd\xe2\x80\x93Steinberg." },
    { DitherAlgorithm::SierraLite,          "Sierra Lite",
        "Minimal 3-tap Sierra variant. Near real-time speed with good tonal fidelity." },
    { DitherAlgorithm::JarvisJudiceNinke,   "Jarvis\xe2\x80\x93Judice\xe2\x80\x93Ninke",
        "12-tap 3-row kernel. Wide diffusion yields exceptional tonal accuracy at the cost of speed." },
    { DitherAlgorithm::Stucki,              "Stucki",
        "JJN variant with adjusted weights. Cleaner shadow detail, slightly sharper edges." },
    { DitherAlgorithm::Bayer,               nullptr,               "Ordered Dithering" }, // header
    { DitherAlgorithm::Bayer,               "Bayer",
        "Recursive threshold matrix. Crisp geometric cross-hatch pattern; scales to any matrix size." },
    { DitherAlgorithm::ClusteredDot,        "Clustered Dot",
        "Dot-cluster threshold map. Mimics analog printing screens; strong halftone look." },
    { DitherAlgorithm::BlueNoise,           "Blue Noise",
        "64\303\22764 void-and-cluster mask. Spectrally optimal; natural, grain-like appearance." },
    { DitherAlgorithm::VoidAndCluster,      "Void and Cluster",
        "32\303\22732 Ulichney optimal mask. Minimal tiling artifacts; visually quiet noise floor." },
    { DitherAlgorithm::DotDiffusion,        nullptr,               "Hybrid"           }, // header
    { DitherAlgorithm::DotDiffusion,        "Dot Diffusion",
        "Knuth (1987) class-matrix scan with error propagation. Clustered dot structure meets tonal accuracy." },
    { DitherAlgorithm::Threshold,           nullptr,               "Tone"             }, // header
    { DitherAlgorithm::Threshold,           "Threshold",
        "Hard black/white cut by a level (no dithering). Pair with Pixel size and Corner radius for smooth poster shapes." },
};

} // namespace

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

            // Build a model so we can insert non-selectable category headers.
            auto* model = new QStandardItemModel(m_algorithm);
            for (const auto& e : kAlgoEntries) {
                const bool isHeader = (e.label == nullptr);
                auto* item = new QStandardItem(isHeader
                    ? QString("— %1 —").arg(QString::fromUtf8(e.description))
                    : QString::fromUtf8(e.label));
                if (isHeader) {
                    item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
                    QFont f = item->font();
                    f.setBold(true);
                    item->setFont(f);
                } else {
                    item->setData(int(e.algo), Qt::UserRole);
                    item->setToolTip(QString::fromUtf8(e.description));
                }
                model->appendRow(item);
            }
            m_algorithm->setModel(model);
            // Select the first real item (skip first header row, which is index 0).
            m_algorithm->setCurrentIndex(1);
            sl->addWidget(m_algorithm);

            // Per-algorithm description label.
            m_description = new QLabel;
            m_description->setWordWrap(true);
            m_description->setObjectName("paramLabel");
            m_description->setStyleSheet(
                "QLabel#paramLabel { color: #808080; font-size: 8pt; padding-top: 2px; }");
            sl->addWidget(m_description);

            m_matrixRow = new QWidget;
            {
                auto* ml = new QVBoxLayout(m_matrixRow);
                ml->setContentsMargins(0, 0, 0, 0);
                ml->setSpacing(4);
                ml->addWidget(makeParamLabel("Matrix size"));
                m_matrix = new NoWheelComboBox;
                m_matrix->addItems({ QString::fromUtf8("2\303\2272"),
                                     QString::fromUtf8("4\303\2274"),
                                     QString::fromUtf8("8\303\2278"),
                                     QString::fromUtf8("16\303\22716") });
                m_matrix->setCurrentIndex(2);
                ml->addWidget(m_matrix);
            }
            m_matrixRow->setVisible(false);
            sl->addWidget(m_matrixRow);

            connect(m_algorithm, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int) {
                const DitherAlgorithm a = currentAlgorithm();
                const bool showMatrix =
                    (a == DitherAlgorithm::Bayer || a == DitherAlgorithm::ClusteredDot);
                const bool isThr = (a == DitherAlgorithm::Threshold);
                m_matrixRow->setVisible(showMatrix);
                m_threshold->setVisible(isThr);
                m_strength->setVisible(!isThr);
                updateDescription();
                fire();
            });
            connect(m_matrix, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int) { fire(); });

            updateDescription();
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
            pl->setSpacing(8);

            m_pixelSize = new SliderRow("Pixel size", 1, 16, 2);
            m_strength  = new SliderRow("Strength",   0, 100, 50);
            m_threshold = new SliderRow("Threshold",  0, 100, 50);
            for (SliderRow* r : { m_pixelSize, m_strength, m_threshold }) {
                r->onValueChanged = [this](int) { fire(); };
                pl->addWidget(r);
            }
            m_threshold->setVisible(false);   // shown only for the Threshold algorithm

            auto* row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(6);
            m_opacity      = new DragSpinBox(":/icons/opacity.svg",       0, 100, 100, "%");
            m_cornerRadius = new DragSpinBox(":/icons/corner_radius.svg", 0,  50,   0, "");
            m_opacity->onValueChanged      = [this](int) { fire(); };
            m_cornerRadius->onValueChanged = [this](int) { fire(); };
            row->addWidget(m_opacity, 1);
            row->addWidget(m_cornerRadius, 1);
            pl->addLayout(row);
        }
        vl->addWidget(new CollapsibleSection("Parameters", paramsContent));
    }

    DitherSettings settings() const
    {
        DitherSettings s;
        s.algorithm = currentAlgorithm();
        static const int sizes[] = { 2, 4, 8, 16 };
        s.bayerSize    = sizes[qBound(0, m_matrix->currentIndex(), 3)];
        s.pixelSize    = m_pixelSize->value();
        s.strength     = m_strength->value();
        s.threshold    = m_threshold->value();
        s.opacity      = m_opacity->value() / 100.0f;
        s.cornerRadius = float(m_cornerRadius->value());
        return s;
    }

    void setSettings(const DitherSettings& s)
    {
        m_updating = true;
        m_algorithm->blockSignals(true);

        // Find the item whose UserRole data matches the algorithm enum value.
        const int target = int(s.algorithm);
        for (int i = 0; i < m_algorithm->count(); ++i) {
            const QVariant data = m_algorithm->itemData(i, Qt::UserRole);
            if (data.isValid() && data.toInt() == target) {
                m_algorithm->setCurrentIndex(i);
                break;
            }
        }
        m_algorithm->blockSignals(false);

        const bool showMatrix = (s.algorithm == DitherAlgorithm::Bayer
                               || s.algorithm == DitherAlgorithm::ClusteredDot);
        const bool isThr = (s.algorithm == DitherAlgorithm::Threshold);
        m_matrixRow->setVisible(showMatrix);
        m_threshold->setVisible(isThr);
        m_strength->setVisible(!isThr);
        updateDescription();

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
        m_threshold->setValue(s.threshold);
        m_opacity->setValue(qRound(s.opacity * 100));
        m_cornerRadius->setValue(qRound(s.cornerRadius));
        m_updating = false;
    }

private:
    void fire() { if (!m_updating && onChanged) onChanged(); }

    DitherAlgorithm currentAlgorithm() const
    {
        const QVariant v = m_algorithm->currentData(Qt::UserRole);
        return v.isValid()
            ? static_cast<DitherAlgorithm>(v.toInt())
            : DitherAlgorithm::FloydSteinberg;
    }

    void updateDescription()
    {
        const DitherAlgorithm a = currentAlgorithm();
        for (const auto& e : kAlgoEntries) {
            if (e.label != nullptr && e.algo == a) {
                m_description->setText(QString::fromUtf8(e.description));
                return;
            }
        }
        m_description->clear();
    }

    NoWheelComboBox* m_algorithm  = nullptr;
    QLabel*          m_description = nullptr;
    QWidget*         m_matrixRow  = nullptr;
    NoWheelComboBox* m_matrix     = nullptr;
    SliderRow*       m_pixelSize  = nullptr;
    SliderRow*       m_strength   = nullptr;
    SliderRow*       m_threshold  = nullptr;
    DragSpinBox*     m_opacity      = nullptr;
    DragSpinBox*     m_cornerRadius = nullptr;
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
            sl->setSpacing(8);

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
            pl->setSpacing(8);

            m_cellSize = new SliderRow("Cell size", 4, 48, 12);
            m_gamma    = new SliderRow("Gamma",    10, 500, 100);
            for (SliderRow* r : { m_cellSize, m_gamma }) {
                r->onValueChanged = [this](int) { fire(); };
                pl->addWidget(r);
            }
        }
        vl->addWidget(new CollapsibleSection("Parameters", paramsContent));
    }

    AsciiSettings settings() const
    {
        AsciiSettings s;
        s.charsetPreset = m_charset->currentIndex();
        s.customCharset = m_customEdit->text();
        s.cellSize      = m_cellSize->value();
        s.gamma         = m_gamma->value() / 100.0f;
        s.invert        = m_invert->isChecked();
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
        m_updating = false;
    }

private:
    void fire() { if (!m_updating && onChanged) onChanged(); }

    NoWheelComboBox* m_charset    = nullptr;
    QLineEdit*       m_customEdit = nullptr;
    QPushButton*     m_invert     = nullptr;
    SliderRow*       m_cellSize   = nullptr;
    SliderRow*       m_gamma      = nullptr;
    bool m_updating = false;
};

// ============================================================
//  ModePanel
// ============================================================

ModePanel::ModePanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("sidePanel");
    setMinimumWidth(Ui::px(420));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── Tabs (rectangle active style) ────────────────────────
    {
        auto* tabRow = new QWidget;
        auto* tl = new QHBoxLayout(tabRow);
        tl->setContentsMargins(Ui::px(40), Ui::px(16), Ui::px(40), Ui::px(12));
        tl->setSpacing(Ui::px(6));

        auto makeTab = [&](const QString& text) {
            auto* b = new QPushButton(text);
            b->setObjectName("rectTab");
            b->setCheckable(true);
            b->setAutoExclusive(true);
            b->setCursor(Qt::PointingHandCursor);
            tl->addWidget(b);
            return b;
        };
        m_tabHalftone = makeTab("Halftone");
        m_tabDither   = makeTab("Dither");
        m_tabAscii    = makeTab("Ascii");
        m_tabHalftone->setChecked(true);
        tl->addStretch(1);

        connect(m_tabHalftone, &QPushButton::clicked, this, [this]() { emit modeSelected(RenderMode::Halftone); });
        connect(m_tabDither,   &QPushButton::clicked, this, [this]() { emit modeSelected(RenderMode::Dither); });
        connect(m_tabAscii,    &QPushButton::clicked, this, [this]() { emit modeSelected(RenderMode::Ascii); });

        outer->addWidget(tabRow);
    }

    // ── Scrollable section stack ─────────────────────────────
    auto* scroll = new QScrollArea;
    scroll->setObjectName("modeScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget;
    content->setObjectName("controlRoot");
    auto* cl = new QVBoxLayout(content);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);

    // Per-mode pages (Shape + Settings).
    m_halftonePage = new HalftonePage;
    m_ditherPage   = new DitherPage;
    m_asciiPage    = new AsciiPage;
    m_ditherPage->setVisible(false);
    m_asciiPage->setVisible(false);

    m_halftonePage->onChanged = [this]() { if (!m_updating) emit paramsChanged(); };
    m_ditherPage->onChanged   = [this]() { if (!m_updating) emit paramsChanged(); };
    m_asciiPage->onChanged    = [this]() { if (!m_updating) emit paramsChanged(); };
    m_halftonePage->onBlendChanged = [this]() {
        if (!m_updating) emit blendChanged(m_halftonePage->blend());
    };

    cl->addWidget(m_halftonePage);
    cl->addWidget(m_ditherPage);
    cl->addWidget(m_asciiPage);

    // ── Fill (shared palette) ────────────────────────────────
    auto* fill = new PanelSection("Fill", /*collapsible*/ true, true);
    // Extend the body to the +/− gutter so the favourite icon can sit in that
    // column; the palette controls compensate with their own right margin.
    fill->body()->setContentsMargins(Ui::px(40), Ui::px(2), Ui::px(24), Ui::px(14));
    m_tonal = new TonalControlsWidget(
        TonalSettings{ ToneMode::FixedTones, defaultAccentTones(1) });
    m_tonal->onChanged = [this]() { if (!m_updating) emit tonalChanged(); };
    fill->body()->addWidget(m_tonal);
    cl->addWidget(fill);

    // ── Stroke (stub: off by default) ────────────────────────
    auto* stroke = new PanelSection("Stroke", /*collapsible*/ true, false);
    {
        auto* sw = new FillSwatch(QColor(0x10, 0x10, 0x10), 1.0f, /*showOpacity*/ false);
        stroke->body()->addWidget(sw);
        auto* thick = new SliderRow("Thickness", 0, 20, 0);
        stroke->body()->addWidget(thick);
    }
    cl->addWidget(stroke);

    // ── Background (shared document colour; "−" removes it) ───
    auto* bg = new PanelSection("Background", /*collapsible*/ true, true);
    m_setBgOpen = [bg](bool open) { bg->setOpen(open); };
    bg->onToggled = [this](bool open) {
        m_bgEnabled = open;                      // collapsed = no background
        if (!m_updating) emit backgroundChanged();
    };
    m_bgSwatch = new FillSwatch(QColor(0x0A, 0x0A, 0x0A), 1.0f, /*showOpacity*/ true);
    m_bgSwatch->onColorEdited = [this](QColor) { if (!m_updating) emit backgroundChanged(); };
    m_bgSwatch->onClicked = [this]() {
        auto* dlg = new ColorPickerDialog(m_bgSwatch->color(), m_bgSwatch->opacity(), true, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->moveNextTo(m_bgSwatch);
        dlg->onColorChanged = [this](QColor c, float a) {
            m_bgSwatch->setColor(c);
            m_bgSwatch->setOpacity(a);
            if (!m_updating) emit backgroundChanged();
        };
        dlg->show(); dlg->raise(); dlg->activateWindow();
    };
    bg->body()->addWidget(m_bgSwatch);
    cl->addWidget(bg);

    // ── Export ────────────────────────────────────────────────
    auto* exp = new PanelSection("Export", /*collapsible*/ true, true);
    {
        auto* labelsRow = new QHBoxLayout;
        labelsRow->setContentsMargins(0, 0, 0, 0);
        labelsRow->setSpacing(Ui::px(10));
        labelsRow->addWidget(makeParamLabel("Output name"), 2);
        labelsRow->addWidget(makeParamLabel("Type of file"), 1);
        exp->body()->addLayout(labelsRow);

        auto* fieldsRow = new QHBoxLayout;
        fieldsRow->setContentsMargins(0, 0, 0, 0);
        fieldsRow->setSpacing(Ui::px(10));
        m_outputName = new QLineEdit("output");
        m_format     = new NoWheelComboBox;
        m_format->addItems({ "SVG", "PNG", "JPG", "PNG Sequence", "MP4" });
        fieldsRow->addWidget(m_outputName, 2);
        fieldsRow->addWidget(m_format, 1);
        exp->body()->addLayout(fieldsRow);

        auto* btnExport = new QPushButton("Export");
        btnExport->setObjectName("accentBtn");
        btnExport->setFixedHeight(Ui::px(44));
        connect(btnExport, &QPushButton::clicked, this, &ModePanel::exportRequested);
        exp->body()->addWidget(btnExport);
    }
    cl->addWidget(exp);

    cl->addSpacing(Ui::px(48));   // breathing room below Export
    cl->addStretch();

    scroll->setWidget(content);
    installAutoHideScrollbar(scroll);
    outer->addWidget(scroll, 1);
}

// ── Fill / Background / source ───────────────────────────────

TonalSettings ModePanel::tonalSettings() const { return m_tonal->settings(); }

void ModePanel::setTonalSettings(const TonalSettings& t)
{
    m_updating = true;
    m_tonal->setSettings(t);
    m_updating = false;
}

void ModePanel::setColorsEnabled(bool enabled) { m_tonal->setEnabled(enabled); }
void ModePanel::setSourceImage(const QImage& img) { m_tonal->setSourceImage(img); }

QColor ModePanel::background()        const { return m_bgSwatch->color(); }
float  ModePanel::backgroundOpacity() const { return m_bgEnabled ? m_bgSwatch->opacity() : 0.0f; }

void ModePanel::setBackground(QColor c, float opacity)
{
    m_updating = true;
    // A fully-transparent background is treated as "removed" → section collapsed.
    m_bgEnabled = (opacity > 0.001f);
    if (m_setBgOpen) m_setBgOpen(m_bgEnabled);
    m_bgSwatch->setColor(c);
    m_bgSwatch->setOpacity(m_bgEnabled ? opacity : 1.0f);
    m_updating = false;
}

HalftoneSettings ModePanel::halftoneSettings() const { return m_halftonePage->settings(); }
DitherSettings   ModePanel::ditherSettings()   const { return m_ditherPage->settings(); }
AsciiSettings    ModePanel::asciiSettings()    const { return m_asciiPage->settings(); }

QString ModePanel::outputFileName() const { return m_outputName->text(); }
QString ModePanel::outputFormat()   const { return m_format->currentText(); }

void ModePanel::setMode(RenderMode m)
{
    m_mode = m;
    m_halftonePage->setVisible(m == RenderMode::Halftone);
    m_ditherPage->setVisible(m == RenderMode::Dither);
    m_asciiPage->setVisible(m == RenderMode::Ascii);
    m_tabHalftone->setChecked(m == RenderMode::Halftone);
    m_tabDither->setChecked(m == RenderMode::Dither);
    m_tabAscii->setChecked(m == RenderMode::Ascii);
}

void ModePanel::setFromLayer(const Layer& layer)
{
    m_updating = true;
    m_halftonePage->setSettings(layer.halftone);
    m_ditherPage->setSettings(layer.dither);
    m_asciiPage->setSettings(layer.ascii);
    m_halftonePage->setBlend(layer.blend);

    // Fill (tonal) mirrors the active layer's mode tonal.
    switch (layer.kind) {
        case LayerKind::Halftone: m_tonal->setSettings(layer.halftone.tonal); break;
        case LayerKind::Dither:   m_tonal->setSettings(layer.dither.tonal);   break;
        case LayerKind::Ascii:    m_tonal->setSettings(layer.ascii.tonal);    break;
        case LayerKind::Original: break;
    }

    if (layer.kind != LayerKind::Original)
        setMode(modeForLayerKind(layer.kind));

    // The Original layer has no mode settings: only the left adjustments apply.
    const bool editable = (layer.kind != LayerKind::Original);
    m_halftonePage->setEnabled(editable);
    m_ditherPage->setEnabled(editable);
    m_asciiPage->setEnabled(editable);
    m_tonal->setEnabled(editable);

    m_updating = false;
}
