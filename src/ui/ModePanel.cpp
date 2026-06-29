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
#include <QFontMetrics>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QStandardItemModel>
#include <QSvgRenderer>
#include <QPainter>
#include <QIcon>
#include <QStyle>

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

// Render a shape SVG as a light silhouette (so it's visible on the dark box),
// used as a live preview of the chosen custom symbol.
QIcon shapePreviewIcon(const QString& path, int sidePx)
{
    QSvgRenderer r(path);
    const int s = sidePx * 2;   // supersample
    QImage img(s, s, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    r.render(&p);
    // Recolour the silhouette to a light tone, keeping its alpha.
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(img.rect(), QColor("#E3E3E3"));
    p.end();
    return QIcon(QPixmap::fromImage(img));
}

// QLabel that shows its text elided with "…" to fit the available width, and
// never forces the row wider (Ignored h-policy) — so a long file name can't
// push neighbours past the column gutter.
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }
    void setFullText(const QString& t) { m_full = t; updateElide(); }
protected:
    void resizeEvent(QResizeEvent* e) override { QLabel::resizeEvent(e); updateElide(); }
private:
    void updateElide()
    {
        QLabel::setText(QFontMetrics(font()).elidedText(m_full, Qt::ElideRight, width()));
    }
    QString m_full;
};

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

bool fillSectionOpenFor(const TonalSettings& tonal)
{
    return tonal.mode == ToneMode::ImageColors || tonal.enabled;
}

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
        // Right gutter reduced from 24 → 14 so the header icons (+/−/x) sit
        // ~5 real px further right (they read slightly off-centre otherwise).
        tl->setContentsMargins(Ui::px(40), Ui::px(12), Ui::px(14), Ui::px(12));
        tl->setSpacing(Ui::px(6));
        tl->addWidget(makeSectionTitle(title), 1);
        m_titleLayout = tl;

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

    // Add an icon button into the title gutter (e.g. a "+" for Shape), placed
    // in the same column as the collapse +/− toggles.
    QPushButton* addHeaderButton(const QString& iconRes)
    {
        auto* b = new QPushButton;
        b->setObjectName("iconBtn");
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(Ui::px(26), Ui::px(26));
        b->setIcon(QIcon(iconRes));
        b->setIconSize(QSize(Ui::px(16), Ui::px(16)));
        m_titleLayout->addWidget(b);
        return b;
    }

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
    QHBoxLayout* m_titleLayout = nullptr;
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

        // ── Shape (one or more shapes; header "+" adds, "−" removes) ──
        m_shapeSection = new PanelSection("Shape", /*collapsible*/ false, true);
        // Body extends to the symbol gutter (24); each row reserves kGutterComp
        // on the right so the combos all share one width and the "−" buttons
        // line up with the section toggles.
        m_shapeSection->body()->setContentsMargins(Ui::px(40), Ui::px(2), Ui::px(24), Ui::px(22));
        m_shapeSection->body()->setSpacing(Ui::px(8));
        m_shapesLayout = m_shapeSection->body();
        {
            auto* addBtn = m_shapeSection->addHeaderButton(":/icons/plus.svg");
            addBtn->setToolTip("Add shape");
            connect(addBtn, &QPushButton::clicked, this, [this]() {
                if (m_shapeSlots.size() < 4) addShapeSlot(HalftoneShape::Circle, QString(), false);
            });
        }
        vl->addWidget(m_shapeSection);
        addShapeSlot(HalftoneShape::Square, QString(), true);

        // ── Settings ────────────────────────────────────────
        auto* settings = new PanelSection("Settings", /*collapsible*/ false, true);
        {
            auto* sl = settings->body();

            sl->addWidget(makeParamLabel("Grid"));
            m_gridType = new NoWheelComboBox;
            m_gridType->addItems({ "Square", "Hexagonal", "Brick", "Wave",
                                   "Radial", "Phyllotaxis" });
            sl->addWidget(m_gridType);
            sl->addSpacing(Ui::px(8));   // breathing room before the sliders

            // All five run on a unified 0..100 UI scale; getSettings/setSettings
            // map each linearly onto its real range (spacing 2..500, rotation
            // 0..360°, gamma 0..5, diameter 0.1..3, jitter 0..1).
            m_spacing      = new SliderRow("Spacing",       0, 100,   4);
            m_rotation     = new SliderRow("Rotation",      0, 100,   0);
            m_gamma        = new SliderRow("Gamma",         0, 100,  20);
            m_diameter     = new SliderRow("Diameter",      0, 100,  31);
            m_weight       = new SliderRow("Weight",        0, 100,   0);
            m_jitter       = new SliderRow("Jitter",        0, 100,   0);
            for (SliderRow* r : { m_spacing, m_rotation, m_gamma, m_diameter,
                                  m_weight, m_jitter }) {
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
            m_cornerRadius = new DragSpinBox(":/icons/corner_radius.svg", 0, 100,   0, "");
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
        for (const auto& slot : m_shapeSlots) {
            ShapeEntry e;
            e.shape   = static_cast<HalftoneShape>(slot.combo->currentIndex());
            e.svgPath = slot.svgPath;
            s.shapes.push_back(e);
        }
        if (s.shapes.empty()) s.shapes.push_back(ShapeEntry{});
        s.multiThreshold = 128;

        s.grid.type          = static_cast<GridType>(m_gridType->currentIndex());
        s.grid.spacing       = 2.0f + m_spacing->value()  / 100.0f * 498.0f;
        s.grid.rotation      = m_rotation->value() / 100.0f * 360.0f;
        s.grid.diameter      = 0.1f + m_diameter->value() / 100.0f * 2.9f;
        // Stretch / stretch-angle removed from the UI: leave grid at identity.

        s.gamma        = m_gamma->value()  / 100.0f * 5.0f;
        s.weight       = m_weight->value() / 100.0f;
        s.jitter       = m_jitter->value() / 100.0f;
        s.opacity      = m_opacity->value() / 100.0f;
        s.cornerRadius = float(m_cornerRadius->value());
        return s;
    }

    // Do the on-screen slots already match these shapes? Shapes aren't animated,
    // so during timeline scrubbing this lets us skip the teardown+rebuild that
    // otherwise collapses and re-expands the panel every frame (worse with more
    // shapes). Numeric sliders below are still updated unconditionally.
    bool shapesMatch(const std::vector<ShapeEntry>& shapes) const
    {
        const int n = shapes.empty() ? 1 : int(shapes.size());
        if (m_shapeSlots.size() != n) return false;
        for (int i = 0; i < int(shapes.size()); ++i)
            if (m_shapeSlots[i].combo->currentIndex() != int(shapes[i].shape)
             || m_shapeSlots[i].svgPath != shapes[i].svgPath) return false;
        return true;
    }

    void setSettings(const HalftoneSettings& s)
    {
        m_updating = true;
        if (!shapesMatch(s.shapes)) {
            clearShapeSlots();
            if (s.shapes.empty())
                addShapeSlot(HalftoneShape::Square, QString(), true);
            else
                for (const auto& e : s.shapes)
                    addShapeSlot(e.shape, e.svgPath, true);
        }

        m_gridType->blockSignals(true);
        m_gridType->setCurrentIndex(int(s.grid.type));
        m_gridType->blockSignals(false);
        m_spacing->setValue(qRound((s.grid.spacing - 2.0f) / 498.0f * 100.0f));
        m_rotation->setValue(qRound(s.grid.rotation / 360.0f * 100.0f));
        m_diameter->setValue(qRound((s.grid.diameter - 0.1f) / 2.9f * 100.0f));
        m_gamma->setValue(qRound(s.gamma / 5.0f * 100.0f));
        m_weight->setValue(qRound(s.weight * 100));
        m_jitter->setValue(qRound(s.jitter * 100));
        m_opacity->setValue(qRound(s.opacity * 100));
        m_cornerRadius->setValue(qRound(s.cornerRadius));
        m_updating = false;
    }

private:
    void fire() { if (!m_updating && onChanged) onChanged(); }

    struct ShapeSlot {
        QWidget*         widget     = nullptr;
        NoWheelComboBox* combo      = nullptr;
        QPushButton*     minusBtn   = nullptr;
        QWidget*         svgRow     = nullptr;   // whole Custom-SVG row (toggled)
        QFrame*          svgNameBox = nullptr;   // left box: preview + name
        QLabel*          svgPreview = nullptr;
        ElidedLabel*     svgName    = nullptr;
        QPushButton*     svgBtn     = nullptr;   // "Choose SVG" / "Change SVG"
        QString          svgPath;
    };

    void refreshMinusButtons()
    {
        for (int i = 0; i < m_shapeSlots.size(); ++i)
            m_shapeSlots[i].minusBtn->setVisible(i > 0);   // first shape isn't removable
    }

    void clearShapeSlots()
    {
        for (auto& slot : m_shapeSlots) {
            m_shapesLayout->removeWidget(slot.widget);
            slot.widget->deleteLater();
        }
        m_shapeSlots.clear();
    }

    // Custom-SVG row state:
    //  · no file  → a single full-width accent "Choose SVG" button.
    //  · loaded   → left box (symbol preview + name) + accent "Change SVG".
    void updateSvgRow(ShapeSlot& slot)
    {
        const bool has = !slot.svgPath.isEmpty();
        slot.svgNameBox->setVisible(has);
        if (has) {
            slot.svgPreview->setPixmap(
                shapePreviewIcon(slot.svgPath, Ui::px(22)).pixmap(Ui::px(22), Ui::px(22)));
            slot.svgName->setFullText(QFileInfo(slot.svgPath).completeBaseName());
            slot.svgBtn->setText("Change SVG");
            // Size to the text so it's never clipped; the name box takes the rest.
            slot.svgBtn->setMinimumWidth(0);
            slot.svgBtn->setMaximumWidth(QWIDGETSIZE_MAX);
            slot.svgBtn->setFixedWidth(slot.svgBtn->sizeHint().width() + Ui::px(6));
        } else {
            slot.svgBtn->setText("Choose SVG");
            slot.svgBtn->setMinimumWidth(0);
            slot.svgBtn->setMaximumWidth(QWIDGETSIZE_MAX);   // span the row
        }
    }

    void addShapeSlot(HalftoneShape shape, const QString& svgPath, bool silent)
    {
        if (m_shapeSlots.size() >= 4) return;
        const int gutterComp = Ui::px(46);   // boxes stop here; symbol sits beyond

        ShapeSlot slot;
        slot.svgPath = svgPath;
        slot.widget  = new QWidget;
        auto* ov = new QVBoxLayout(slot.widget);
        ov->setContentsMargins(0, 0, 0, 0);
        ov->setSpacing(Ui::px(6));

        // Combo (capped at the box gutter) + "−" in the symbol gutter.
        auto* rowl = new QHBoxLayout;
        rowl->setContentsMargins(0, 0, 0, 0);
        rowl->setSpacing(0);
        slot.combo = new NoWheelComboBox;
        slot.combo->addItems({ "Triangle", "Circle", "Square", "Star", "Custom SVG" });
        slot.combo->setCurrentIndex(int(shape));
        rowl->addWidget(slot.combo, 1);

        auto* gut = new QWidget;
        gut->setFixedWidth(gutterComp);
        auto* gl = new QHBoxLayout(gut);
        gl->setContentsMargins(0, 0, 0, 0);
        gl->setSpacing(0);
        gl->addStretch(1);
        slot.minusBtn = new QPushButton;
        slot.minusBtn->setObjectName("iconBtn");
        slot.minusBtn->setCursor(Qt::PointingHandCursor);
        slot.minusBtn->setFixedSize(Ui::px(26), Ui::px(26));
        slot.minusBtn->setIcon(QIcon(":/icons/minus.svg"));
        slot.minusBtn->setIconSize(QSize(Ui::px(16), Ui::px(16)));
        gl->addWidget(slot.minusBtn);
        rowl->addWidget(gut);
        ov->addLayout(rowl);

        // SVG row: a left preview+name box and an accent action button. Aligns
        // with the combo above (stops at the same box gutter, not the symbol one).
        slot.svgRow = new QWidget;
        auto* sr = new QHBoxLayout(slot.svgRow);
        sr->setContentsMargins(0, 0, gutterComp, 0);
        sr->setSpacing(Ui::px(8));

        slot.svgNameBox = new QFrame;
        slot.svgNameBox->setObjectName("dragSpinBox");          // dark box + border
        slot.svgNameBox->setFixedHeight(Ui::px(48));            // == combos / other boxes
        auto* nbl = new QHBoxLayout(slot.svgNameBox);
        nbl->setContentsMargins(Ui::px(10), 0, Ui::px(10), 0);
        nbl->setSpacing(Ui::px(8));
        slot.svgPreview = new QLabel;
        slot.svgPreview->setFixedSize(Ui::px(22), Ui::px(22));
        slot.svgPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
        nbl->addWidget(slot.svgPreview);
        slot.svgName = new ElidedLabel;
        slot.svgName->setStyleSheet("background:transparent; color:#E3E3E3;");
        slot.svgName->setAttribute(Qt::WA_TransparentForMouseEvents);
        nbl->addWidget(slot.svgName, 1);
        sr->addWidget(slot.svgNameBox, 1);

        slot.svgBtn = new QPushButton;
        slot.svgBtn->setObjectName("accentBtn");                // orange (colour via QSS)
        slot.svgBtn->setCursor(Qt::PointingHandCursor);
        // Pin the height to match the name box: #accentBtn carries a literal
        // min-height:40px (unscaled) that would otherwise mis-size it.
        slot.svgBtn->setStyleSheet(QString("min-height:%1px; max-height:%1px;").arg(Ui::px(48)));
        slot.svgBtn->setFixedHeight(Ui::px(48));
        sr->addWidget(slot.svgBtn, 1);

        updateSvgRow(slot);
        slot.svgRow->setVisible(shape == HalftoneShape::CustomSVG);
        ov->addWidget(slot.svgRow);

        m_shapeSlots.append(slot);
        m_shapesLayout->addWidget(slot.widget);
        refreshMinusButtons();

        QWidget*         w      = slot.widget;
        NoWheelComboBox* combo  = slot.combo;
        QWidget*         svgRow = slot.svgRow;
        QPushButton*     svgBtn = slot.svgBtn;
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, svgRow](int idx) {
            svgRow->setVisible(idx == int(HalftoneShape::CustomSVG));
            fire();
        });
        connect(slot.minusBtn, &QPushButton::clicked, this, [this, w]() { removeShapeSlot(w); });
        connect(svgBtn, &QPushButton::clicked, this, [this, w]() {
            const QString p = QFileDialog::getOpenFileName(this, "Load SVG", "", "SVG files (*.svg)");
            if (p.isEmpty()) return;
            for (auto& s : m_shapeSlots)
                if (s.widget == w) { s.svgPath = p; updateSvgRow(s); break; }
            fire();
        });

        if (!silent) fire();
    }

    void removeShapeSlot(QWidget* w)
    {
        if (m_shapeSlots.size() <= 1) return;
        for (int i = 0; i < m_shapeSlots.size(); ++i)
            if (m_shapeSlots[i].widget == w) {
                m_shapesLayout->removeWidget(w);
                w->deleteLater();
                m_shapeSlots.removeAt(i);
                break;
            }
        refreshMinusButtons();
        fire();
    }

    PanelSection*      m_shapeSection = nullptr;
    QVBoxLayout*       m_shapesLayout = nullptr;
    QVector<ShapeSlot> m_shapeSlots;

    NoWheelComboBox* m_gridType     = nullptr;
    SliderRow*       m_spacing      = nullptr;
    SliderRow*       m_rotation     = nullptr;
    SliderRow*       m_gamma        = nullptr;
    SliderRow*       m_diameter     = nullptr;
    SliderRow*       m_weight       = nullptr;
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
    { DitherAlgorithm::Ostromoukhov,        "Ostromoukhov",
        "Variable-coefficient diffusion: weights adapt to each tone. Very clean, even gradients with few artifacts. (tables from libdither)" },
    { DitherAlgorithm::Riemersma,           "Riemersma",
        "Diffuses error along a Hilbert space-filling curve. Distinctive, locally-clustered grain with no directional banding. (method from libdither)" },
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
            m_cornerRadius = new DragSpinBox(":/icons/corner_radius.svg", 0, 100,   0, "");
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
        // Right gutter 24→14 so the "X" lines up with the section header icons.
        tl->setContentsMargins(Ui::px(24), Ui::px(16), Ui::px(14), Ui::px(12));
        tl->setSpacing(Ui::px(4));

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

        // "X" in the +/- gutter: clears the mode → layer goes back to Original
        // (raw image), a second way to deselect besides the row context menu.
        auto* clearBtn = new QPushButton;
        clearBtn->setObjectName("iconBtn");
        clearBtn->setCursor(Qt::PointingHandCursor);
        clearBtn->setFixedSize(Ui::px(26), Ui::px(26));
        clearBtn->setIconSize(QSize(Ui::px(16), Ui::px(16)));
        clearBtn->setIcon(QIcon(":/icons/x.svg"));
        clearBtn->setToolTip("Clear mode (show original)");
        connect(clearBtn, &QPushButton::clicked, this, &ModePanel::clearModeRequested);
        tl->addWidget(clearBtn);

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
    m_fillSection = fill;
    // Extend the body to the +/− gutter so the favourite icon can sit in that
    // column; the palette controls compensate with their own right margin.
    // Right margin 24→14 to match the (shifted) header icons; TonalControls'
    // kGutterComp tracks this so the boxes still stop at the 70px gutter.
    fill->body()->setContentsMargins(Ui::px(40), Ui::px(2), Ui::px(14), Ui::px(14));
    m_tonal = new TonalControlsWidget(
        TonalSettings{ ToneMode::FixedTones, defaultAccentTones(1) });
    m_tonal->onChanged = [this]() { if (!m_updating) emit tonalChanged(); };
    fill->body()->addWidget(m_tonal);
    // The "−" removes the fill (collapsed = no fill); "+" restores it.
    m_setFillOpen = [fill](bool open) { fill->setOpen(open); };
    fill->onToggled = [this](bool open) {
        m_fillEnabled = open;
        if (!m_updating) emit tonalChanged();
    };
    cl->addWidget(fill);

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
        // Just the file type + the Export button. The output name comes from
        // the document title (top-left), so no name field is needed here.
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(Ui::px(14));
        m_format = new NoWheelComboBox;
        m_format->addItems({ "PNG", "PNG Sequence", "JPG", "MP4", "SVG" });
        m_format->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_format->setMinimumWidth(Ui::px(60));   // allow the combo to shrink
        row->addWidget(m_format, 1);

        auto* btnExport = new QPushButton("Export");
        btnExport->setObjectName("accentBtn");
        // Override the qss min-height (raw 40px) so it matches the combo height.
        btnExport->setStyleSheet(QString("QPushButton#accentBtn{min-height:%1px;}").arg(Ui::px(48)));
        btnExport->setFixedHeight(Ui::px(48));
        btnExport->setMinimumWidth(Ui::px(110));
        btnExport->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        connect(btnExport, &QPushButton::clicked, this, &ModePanel::exportRequested);
        row->addWidget(btnExport, 1);
        exp->body()->addLayout(row);
    }
    cl->addWidget(exp);

    cl->addSpacing(Ui::px(48));   // breathing room below Export
    cl->addStretch();

    scroll->setWidget(content);
    installOverlayScrollbar(scroll);   // floats → dividers reach the panel edge
    outer->addWidget(scroll, 1);
}

// ── Fill / Background / source ───────────────────────────────

TonalSettings ModePanel::tonalSettings() const
{
    TonalSettings t = m_tonal->settings();
    t.enabled = m_fillEnabled || t.mode == ToneMode::ImageColors;
    return t;
}

void ModePanel::setTonalSettings(const TonalSettings& t)
{
    m_updating = true;
    m_fillEnabled = fillSectionOpenFor(t);
    if (m_setFillOpen) m_setFillOpen(m_fillEnabled);
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
    TonalSettings tonal;
    bool haveTonal = true;
    switch (layer.kind) {
        case LayerKind::Halftone: tonal = layer.halftone.tonal; break;
        case LayerKind::Dither:   tonal = layer.dither.tonal;   break;
        case LayerKind::Ascii:    tonal = layer.ascii.tonal;    break;
        case LayerKind::Original: haveTonal = false; break;
    }
    if (haveTonal) {
        m_fillEnabled = fillSectionOpenFor(tonal);
        if (m_setFillOpen) m_setFillOpen(m_fillEnabled);
        m_tonal->setSettings(tonal);
    }

    // The Original (clean) layer has no mode: the tabs stay visible (so a mode
    // can be re-picked) but everything tied to a mode disappears — the settings
    // page and the colour/Fill section. Only the left adjustments apply.
    const bool hasMode = (layer.kind != LayerKind::Original);
    if (hasMode) setMode(modeForLayerKind(layer.kind));
    else {
        // autoExclusive refuses to uncheck the last checked tab, so drop it
        // momentarily to clear the highlight, then restore exclusivity.
        for (auto* t : { m_tabHalftone, m_tabDither, m_tabAscii }) {
            t->setAutoExclusive(false);
            t->setChecked(false);
            t->setAutoExclusive(true);
        }
        m_halftonePage->setVisible(false);
        m_ditherPage->setVisible(false);
        m_asciiPage->setVisible(false);
    }
    m_fillSection->setVisible(hasMode);

    m_updating = false;
}
