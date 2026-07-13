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
#include <QFontDatabase>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
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

// Photoshop-style group name for the blend mode that starts each group.
QString blendGroupName(BlendMode m)
{
    switch (m) {
        case BlendMode::Darken:     return "Darken";
        case BlendMode::Lighten:    return "Lighten";
        case BlendMode::Overlay:    return "Contrast";
        case BlendMode::Difference: return "Comparative";
        case BlendMode::Hue:        return "Component";
        default:                    return QString();
    }
}

// Fusion picker entries, shared by every mode page's Fusion control.
QVector<PopupPickerEntry> blendPickerEntries()
{
    QVector<PopupPickerEntry> out;
    for (const auto& e : kBlend) {
        if (out.isEmpty()) out.push_back({ QVariant(), QString(), "Normal", QString() });
        if (e.groupStart)  out.push_back({ QVariant(), QString(), blendGroupName(e.mode), QString() });
        out.push_back({ int(e.mode), QString::fromUtf8(e.name), QString(), QString() });
    }
    return out;
}

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
            m_gridType = new PopupPicker(1);
            m_gridType->setEntries({
                { int(GridType::Square),      "Square",      QString(), QString() },
                { int(GridType::Hexagonal),   "Hexagonal",   QString(), QString() },
                { int(GridType::Brick),       "Brick",       QString(), QString() },
                { int(GridType::Wave),        "Wave",        QString(), QString() },
                { int(GridType::Radial),      "Radial",      QString(), QString() },
                { int(GridType::Phyllotaxis), "Phyllotaxis", QString(), QString() },
            });
            m_gridType->setValue(int(GridType::Square));
            m_gridType->onSelected = [this](QVariant) { fire(); };
            sl->addWidget(m_gridType);
            sl->addSpacing(Ui::px(8));   // breathing room before the sliders

            // Spacing is shown in its real units (px) so the box matches the
            // rendered grid. The rest run on a unified 0..100 UI scale that
            // getSettings/setSettings map onto their real range (rotation
            // 0..360°, gamma 0..5, diameter 0.1..3, jitter 0..1).
            m_spacing      = new SliderRow("Spacing",       2, 200,  50);
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
            m_fusion = new PopupPicker(1);
            m_fusion->setEntries(blendPickerEntries());
            m_fusion->setValue(int(BlendMode::Normal));
            m_fusion->onSelected = [this](QVariant) {
                if (!m_updating && onBlendChanged) onBlendChanged();
            };
            sl->addWidget(m_fusion);

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
        }
        vl->addWidget(settings);
    }

    BlendMode blend() const { return BlendMode(m_fusion->value().toInt()); }
    void setBlend(BlendMode m) { m_fusion->setValue(int(m)); }

    void setAnimatedParams(const QSet<ParamId>& ids)
    {
        m_spacing->setAnimated(ids.contains(ParamId::HtGridSpacing));
        m_rotation->setAnimated(ids.contains(ParamId::HtGridRotation));
        m_gamma->setAnimated(ids.contains(ParamId::HtGamma));
        m_diameter->setAnimated(ids.contains(ParamId::HtGridDiameter));
        m_weight->setAnimated(ids.contains(ParamId::HtWeight));
        m_jitter->setAnimated(ids.contains(ParamId::HtJitter));
        m_opacity->setAnimated(ids.contains(ParamId::HtOpacity));
        m_cornerRadius->setAnimated(ids.contains(ParamId::HtCornerRadius));
    }

    QHash<QWidget*, ParamId> paramWidgets() const
    {
        return {
            { m_spacing, ParamId::HtGridSpacing }, { m_rotation, ParamId::HtGridRotation },
            { m_gamma, ParamId::HtGamma },         { m_diameter, ParamId::HtGridDiameter },
            { m_weight, ParamId::HtWeight },       { m_jitter, ParamId::HtJitter },
            { m_opacity, ParamId::HtOpacity },     { m_cornerRadius, ParamId::HtCornerRadius },
        };
    }

    HalftoneSettings settings() const
    {
        HalftoneSettings s;
        s.inputDpi = 300;                       // fixed high quality (no DPI control)
        s.shapes.clear();
        for (const auto& slot : m_shapeSlots) {
            ShapeEntry e;
            e.shape   = static_cast<HalftoneShape>(slot.combo->value().toInt());
            e.svgPath = slot.svgPath;
            s.shapes.push_back(e);
        }
        if (s.shapes.empty()) s.shapes.push_back(ShapeEntry{});
        s.multiThreshold = 128;

        s.grid.type          = static_cast<GridType>(m_gridType->value().toInt());
        s.grid.spacing       = float(m_spacing->value());   // real px (matches UI)
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
            if (m_shapeSlots[i].combo->value().toInt() != int(shapes[i].shape)
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

        m_gridType->setValue(int(s.grid.type));   // silent
        m_spacing->setValue(qRound(s.grid.spacing));
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
        PopupPicker*     combo      = nullptr;
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
        slot.combo = new PopupPicker(1);
        slot.combo->setEntries({
            { int(HalftoneShape::Triangle),  "Triangle",   QString(), QString() },
            { int(HalftoneShape::Circle),    "Circle",     QString(), QString() },
            { int(HalftoneShape::Square),    "Square",     QString(), QString() },
            { int(HalftoneShape::Star),      "Star",       QString(), QString() },
            { int(HalftoneShape::CustomSVG), "Custom SVG", QString(), QString() },
        });
        slot.combo->setValue(int(shape));
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
        PopupPicker*     combo  = slot.combo;
        QWidget*         svgRow = slot.svgRow;
        QPushButton*     svgBtn = slot.svgBtn;
        combo->onSelected = [this, svgRow](QVariant v) {
            svgRow->setVisible(v.toInt() == int(HalftoneShape::CustomSVG));
            fire();
        };
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

    PopupPicker*     m_gridType     = nullptr;
    SliderRow*       m_spacing      = nullptr;
    SliderRow*       m_rotation     = nullptr;
    SliderRow*       m_gamma        = nullptr;
    SliderRow*       m_diameter     = nullptr;
    SliderRow*       m_weight       = nullptr;
    SliderRow*       m_jitter       = nullptr;
    PopupPicker*     m_fusion       = nullptr;
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
    { DitherAlgorithm::FalseFloydSteinberg, "False Floyd",
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
    { DitherAlgorithm::LineHatch,           "Line Hatch",
        "Parallel line screen: line thickness follows tone. Set the angle and spacing for an engraving / hatching look." },
    { DitherAlgorithm::CustomPattern,       "Custom Pattern",
        "Any image, tiled as the threshold matrix (rank-normalised). Pick a texture and the tones dither through it." },
    { DitherAlgorithm::DotDiffusion,        nullptr,               "Other"            }, // header
    { DitherAlgorithm::DotDiffusion,        "Dot Diffusion",
        "Knuth (1987) class-matrix scan with error propagation. Clustered dot structure meets tonal accuracy." },
    { DitherAlgorithm::Threshold,           "Threshold",
        "Hard black/white cut by a level (no dithering). Pair with Pixel size and Corner radius for smooth poster shapes." },
};

// Dither algorithm picker entries (drives the shared PopupPicker widget).
QVector<PopupPickerEntry> algoPickerEntries()
{
    QVector<PopupPickerEntry> out;
    for (const auto& e : kAlgoEntries) {
        if (e.label == nullptr)
            out.push_back({ QVariant(), QString(), QString::fromUtf8(e.description), QString() });
        else
            out.push_back({ int(e.algo), QString::fromUtf8(e.label), QString(),
                             QString::fromUtf8(e.description) });
    }
    return out;
}

} // namespace

class DitherPage : public QWidget
{
public:
    std::function<void()> onChanged;
    std::function<void()> onBlendChanged;

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
            m_algorithm = new PopupPicker(1);
            m_algorithm->setEntries(algoPickerEntries());
            m_algorithm->setValue(int(DitherAlgorithm::FloydSteinberg));
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

            // Scan direction — error-diffusion algorithms only.
            m_scanRow = new QWidget;
            {
                auto* cl = new QVBoxLayout(m_scanRow);
                cl->setContentsMargins(0, 0, 0, 0);
                cl->setSpacing(4);
                cl->addWidget(makeParamLabel("Scan direction"));
                m_scan = new NoWheelComboBox;
                m_scan->addItems({ "Serpentine (zig-zag)",
                                   QString::fromUtf8("Standard (left \342\206\222 right)") });
                cl->addWidget(m_scan);
            }
            sl->addWidget(m_scanRow);

            // Line Hatch controls.
            m_lineRow = new QWidget;
            {
                auto* ll = new QVBoxLayout(m_lineRow);
                ll->setContentsMargins(0, 0, 0, 0);
                ll->setSpacing(8);
                m_lineAngle   = new SliderRow("Line angle",   0, 180, 45);
                m_lineSpacing = new SliderRow("Line spacing", 2,  32,  6);
                m_lineAngle->onValueChanged   = [this](int) { fire(); };
                m_lineSpacing->onValueChanged = [this](int) { fire(); };
                ll->addWidget(m_lineAngle);
                ll->addWidget(m_lineSpacing);
            }
            m_lineRow->setVisible(false);
            sl->addWidget(m_lineRow);

            // Custom Pattern picker (mirrors the halftone Custom SVG row).
            m_patternRow = new QWidget;
            {
                auto* pr = new QHBoxLayout(m_patternRow);
                pr->setContentsMargins(0, 0, 0, 0);
                pr->setSpacing(Ui::px(8));

                m_patternNameBox = new QFrame;
                m_patternNameBox->setObjectName("dragSpinBox");   // dark box + border
                m_patternNameBox->setFixedHeight(Ui::px(48));
                auto* nbl = new QHBoxLayout(m_patternNameBox);
                nbl->setContentsMargins(Ui::px(10), 0, Ui::px(10), 0);
                nbl->setSpacing(Ui::px(8));
                m_patternPreview = new QLabel;
                m_patternPreview->setFixedSize(Ui::px(22), Ui::px(22));
                m_patternPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
                nbl->addWidget(m_patternPreview);
                m_patternName = new ElidedLabel;
                m_patternName->setStyleSheet("background:transparent; color:#E3E3E3;");
                m_patternName->setAttribute(Qt::WA_TransparentForMouseEvents);
                nbl->addWidget(m_patternName, 1);
                pr->addWidget(m_patternNameBox, 1);

                m_patternBtn = new QPushButton;
                m_patternBtn->setObjectName("accentBtn");
                m_patternBtn->setCursor(Qt::PointingHandCursor);
                m_patternBtn->setStyleSheet(
                    QString("min-height:%1px; max-height:%1px;").arg(Ui::px(48)));
                m_patternBtn->setFixedHeight(Ui::px(48));
                pr->addWidget(m_patternBtn, 1);

                connect(m_patternBtn, &QPushButton::clicked, this, [this]() {
                    const QString p = QFileDialog::getOpenFileName(
                        this, "Load pattern image", "",
                        "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
                    if (p.isEmpty()) return;
                    m_patternPath = p;
                    updatePatternRow();
                    fire();
                });
            }
            m_patternRow->setVisible(false);
            updatePatternRow();
            sl->addWidget(m_patternRow);

            m_algorithm->onSelected = [this](QVariant) {
                updateConditionalRows();
                updateDescription();
                fire();
            };
            connect(m_matrix, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int) { fire(); });
            connect(m_scan, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int) { fire(); });

            updateDescription();
        }
        // PanelSection (same as the Halftone page) so titles, band lines and
        // gutters match across the three modes.
        auto* settingsSec = new PanelSection("Settings", /*collapsible*/ false, true);
        settingsSec->body()->addWidget(settingsContent);
        vl->addWidget(settingsSec);

        // ── Parameters ──────────────────────────────────────
        auto* paramsContent = new QWidget;
        {
            auto* pl = new QVBoxLayout(paramsContent);
            pl->setContentsMargins(0, 0, 0, 0);
            pl->setSpacing(8);

            m_pixelSize = new SliderRow("Pixel size", 1, 16, 2);
            m_strength  = new SliderRow("Strength",   0, 100, 50);
            m_threshold = new SliderRow("Threshold",  0, 100, 50);
            m_levels    = new SliderRow("Color levels", 2, 16, 2);
            m_levels->setToolTip("Image colors: quantisation steps per RGB channel.\n"
                                 "Palette: intermediate mixing colors between palette entries.");
            for (SliderRow* r : { m_pixelSize, m_strength, m_threshold, m_levels }) {
                r->onValueChanged = [this](int) { fire(); };
                pl->addWidget(r);
            }
            m_threshold->setVisible(false);   // shown only for the Threshold algorithm

            pl->addWidget(makeParamLabel("Fusion"));
            m_fusion = new PopupPicker(1);
            m_fusion->setEntries(blendPickerEntries());
            m_fusion->setValue(int(BlendMode::Normal));
            m_fusion->onSelected = [this](QVariant) {
                if (!m_updating && onBlendChanged) onBlendChanged();
            };
            pl->addWidget(m_fusion);

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
        auto* paramsSec = new PanelSection("Parameters", /*collapsible*/ false, true);
        paramsSec->body()->addWidget(paramsContent);
        vl->addWidget(paramsSec);
    }

    void setAnimatedParams(const QSet<ParamId>& ids)
    {
        m_pixelSize->setAnimated(ids.contains(ParamId::DiPixelSize));
        m_strength->setAnimated(ids.contains(ParamId::DiStrength));
        m_threshold->setAnimated(ids.contains(ParamId::DiThreshold));
        m_levels->setAnimated(ids.contains(ParamId::DiLevels));
        m_lineAngle->setAnimated(ids.contains(ParamId::DiLineAngle));
        m_lineSpacing->setAnimated(ids.contains(ParamId::DiLineSpacing));
        m_opacity->setAnimated(ids.contains(ParamId::DiOpacity));
        m_cornerRadius->setAnimated(ids.contains(ParamId::DiCornerRadius));
    }

    QHash<QWidget*, ParamId> paramWidgets() const
    {
        return {
            { m_pixelSize, ParamId::DiPixelSize }, { m_strength, ParamId::DiStrength },
            { m_threshold, ParamId::DiThreshold }, { m_levels, ParamId::DiLevels },
            { m_lineAngle, ParamId::DiLineAngle }, { m_lineSpacing, ParamId::DiLineSpacing },
            { m_opacity, ParamId::DiOpacity },     { m_cornerRadius, ParamId::DiCornerRadius },
        };
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
        s.levels       = m_levels->value();
        s.serpentine   = (m_scan->currentIndex() == 0);
        s.lineAngle    = float(m_lineAngle->value());
        s.lineSpacing  = m_lineSpacing->value();
        s.patternPath  = m_patternPath;
        return s;
    }

    void setSettings(const DitherSettings& s)
    {
        m_updating = true;
        m_algorithm->setValue(int(s.algorithm));   // silent

        updateConditionalRows();
        updateDescription();

        int mi = 2;
        if      (s.bayerSize == 2)  mi = 0;
        else if (s.bayerSize == 4)  mi = 1;
        else if (s.bayerSize == 8)  mi = 2;
        else if (s.bayerSize == 16) mi = 3;
        m_matrix->blockSignals(true);
        m_matrix->setCurrentIndex(mi);
        m_matrix->blockSignals(false);

        m_scan->blockSignals(true);
        m_scan->setCurrentIndex(s.serpentine ? 0 : 1);
        m_scan->blockSignals(false);

        m_pixelSize->setValue(s.pixelSize);
        m_strength->setValue(s.strength);
        m_threshold->setValue(s.threshold);
        m_opacity->setValue(qRound(s.opacity * 100));
        m_cornerRadius->setValue(qRound(s.cornerRadius));
        m_levels->setValue(s.levels);
        m_lineAngle->setValue(qRound(s.lineAngle));
        m_lineSpacing->setValue(s.lineSpacing);
        if (m_patternPath != s.patternPath) {
            m_patternPath = s.patternPath;
            updatePatternRow();
        }
        m_updating = false;
    }

    BlendMode blend() const { return BlendMode(m_fusion->value().toInt()); }
    void setBlend(BlendMode m) { m_fusion->setValue(int(m)); }

private:
    void fire() { if (!m_updating && onChanged) onChanged(); }

    DitherAlgorithm currentAlgorithm() const { return DitherAlgorithm(m_algorithm->value().toInt()); }

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

    // Show only the rows that apply to the current algorithm.
    void updateConditionalRows()
    {
        const DitherAlgorithm a = currentAlgorithm();
        const bool isThr = (a == DitherAlgorithm::Threshold);
        const bool showScan =
               a == DitherAlgorithm::FloydSteinberg
            || a == DitherAlgorithm::FalseFloydSteinberg
            || a == DitherAlgorithm::Atkinson
            || a == DitherAlgorithm::Burkes
            || a == DitherAlgorithm::Sierra
            || a == DitherAlgorithm::SierraLite
            || a == DitherAlgorithm::JarvisJudiceNinke
            || a == DitherAlgorithm::Stucki
            || a == DitherAlgorithm::Ostromoukhov;
        m_matrixRow->setVisible(a == DitherAlgorithm::Bayer
                             || a == DitherAlgorithm::ClusteredDot);
        m_scanRow->setVisible(showScan);
        m_lineRow->setVisible(a == DitherAlgorithm::LineHatch);
        m_patternRow->setVisible(a == DitherAlgorithm::CustomPattern);
        m_threshold->setVisible(isThr);
        m_strength->setVisible(!isThr);
        // renderThreshold() takes a hard cut straight from s.tonal.tones and
        // never reads s.levels (see DitherRenderer.cpp) — dragging this would
        // have zero visible effect, so hide it rather than mislead.
        m_levels->setVisible(!isThr);
    }

    // No file → full-width accent "Choose image"; loaded → thumb + name box.
    void updatePatternRow()
    {
        const bool has = !m_patternPath.isEmpty();
        m_patternNameBox->setVisible(has);
        if (has) {
            m_patternPreview->setPixmap(QPixmap(m_patternPath).scaled(
                Ui::px(22), Ui::px(22),
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_patternName->setFullText(QFileInfo(m_patternPath).completeBaseName());
            m_patternBtn->setText("Change image");
        } else {
            m_patternBtn->setText("Choose image");
        }
    }

    PopupPicker*     m_algorithm  = nullptr;
    QLabel*          m_description = nullptr;
    QWidget*         m_matrixRow  = nullptr;
    NoWheelComboBox* m_matrix     = nullptr;
    QWidget*         m_scanRow    = nullptr;
    NoWheelComboBox* m_scan       = nullptr;
    QWidget*         m_lineRow     = nullptr;
    SliderRow*       m_lineAngle   = nullptr;
    SliderRow*       m_lineSpacing = nullptr;
    QWidget*         m_patternRow     = nullptr;
    QFrame*          m_patternNameBox = nullptr;
    QLabel*          m_patternPreview = nullptr;
    ElidedLabel*     m_patternName    = nullptr;
    QPushButton*     m_patternBtn     = nullptr;
    QString          m_patternPath;
    SliderRow*       m_pixelSize  = nullptr;
    SliderRow*       m_strength   = nullptr;
    SliderRow*       m_threshold  = nullptr;
    SliderRow*       m_levels     = nullptr;
    PopupPicker*     m_fusion       = nullptr;
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
    std::function<void()> onBlendChanged;

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
            m_charset = new PopupPicker(1);
            {
                QVector<PopupPickerEntry> entries;
                int idx = 0;
                for (const auto& preset : asciiCharsetPresets())
                    entries.push_back({ idx++, preset.name, QString(), preset.chars });
                entries.push_back({ idx, "Custom", QString(), QString() });
                m_charset->setEntries(entries);
                m_charset->setValue(0);
            }
            sl->addWidget(m_charset);

            m_customEdit = new QLineEdit;
            m_customEdit->setPlaceholderText("Light → dark characters…");
            m_customEdit->setVisible(false);
            sl->addWidget(m_customEdit);

            // Font family + weight. Fixed-pitch families plus the bundled
            // display font; coverage measurement adapts to any of them.
            sl->addWidget(makeParamLabel("Font"));
            auto* fontRow = new QHBoxLayout;
            fontRow->setContentsMargins(0, 0, 0, 0);
            fontRow->setSpacing(6);
            m_font = new PopupPicker(1);
            {
                QStringList fams;
                for (const QString& f : QFontDatabase::families())
                    if (QFontDatabase::isFixedPitch(f) && !f.startsWith('@'))
                        fams << f;
                if (!fams.contains("Funnel Display")) fams << "Funnel Display";
                fams.sort(Qt::CaseInsensitive);
                QVector<PopupPickerEntry> entries;
                for (const QString& f : fams)
                    entries.push_back({ f, f, QString(), QString() });
                m_font->setEntries(entries);
                m_font->setValue("Consolas");
            }
            m_font->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            m_font->setMinimumWidth(Ui::px(60));
            m_weight = new NoWheelComboBox;
            m_weight->addItems({ "Regular", "Medium", "DemiBold", "Bold" });
            m_weight->setCurrentIndex(2);
            m_weight->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            m_weight->setMinimumContentsLength(6);
            m_weight->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            m_weight->setMinimumWidth(Ui::px(60));
            fontRow->addWidget(m_font, 2);
            fontRow->addWidget(m_weight, 1);
            sl->addLayout(fontRow);

            m_charset->onSelected = [this](QVariant v) {
                m_customEdit->setVisible(v.toInt() >= int(asciiCharsetPresets().size()));
                fire();
            };
            connect(m_customEdit, &QLineEdit::textChanged, this, [this](const QString&) { fire(); });
            m_font->onSelected = [this](QVariant) { fire(); };
            connect(m_weight, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int) { fire(); });
        }
        auto* settingsSec = new PanelSection("Settings", /*collapsible*/ false, true);
        settingsSec->body()->addWidget(settingsContent);
        vl->addWidget(settingsSec);

        // ── Parameters ──────────────────────────────────────
        auto* paramsContent = new QWidget;
        {
            auto* pl = new QVBoxLayout(paramsContent);
            pl->setContentsMargins(0, 0, 0, 0);
            pl->setSpacing(8);

            m_cellSize = new SliderRow("Cell size", 4, 48, 12);
            m_gamma    = new SliderRow("Gamma",    10, 500, 100);
            m_edges    = new SliderRow("Edges",     0, 100,  0);
            m_edges->setToolTip("Contour glyphs (- / | \\) on detected edges.\n"
                                "0 = off; higher picks up softer edges.");
            m_hatching = new SliderRow("Hatching",  0, 100,  0);
            m_hatching->setToolTip("Directional strokes shade the shadows, like\n"
                                   "copper-engraving cross-hatch. 0 = off.");
            m_stipple  = new SliderRow("Stipple",   0, 100,  0);
            m_stipple->setToolTip("Organic per-cell darkness jitter — breaks up\n"
                                  "clean ramp bands into a hand-stippled scatter.");
            m_contour  = new SliderRow("Contour",   0, 100,  0);
            m_contour->setToolTip("Isoline-only mask (topographic-map look): only\n"
                                  "tonal-band edges draw, flat regions stay blank.");
            for (SliderRow* r : { m_cellSize, m_gamma, m_edges, m_hatching, m_stipple, m_contour }) {
                r->onValueChanged = [this](int) { fire(); };
                pl->addWidget(r);
            }

            m_orderedDither = new QPushButton("Ordered dither");
            m_orderedDither->setCheckable(true);
            m_orderedDither->setCursor(Qt::PointingHandCursor);
            m_orderedDither->setFixedHeight(Ui::px(48));
            m_orderedDither->setToolTip("Bayer-threshold glyph pick instead of nearest-\n"
                                       "coverage — smoother gradients, no denser charset.");
            m_orderedDither->setStyleSheet(QString(
                "QPushButton{background:#3B3B3B;border:1px solid #5D5D5D;border-radius:%1px;"
                "color:#B2B2B2;font-size:%2px;font-weight:500;}"
                "QPushButton:hover{border-color:#828282;}"
                "QPushButton:checked{background:#484848;border-color:#828282;color:#E3E3E3;}")
                .arg(Ui::px(8)).arg(Ui::px(18)));
            connect(m_orderedDither, &QPushButton::toggled, this, [this](bool) { fire(); });
            pl->addWidget(m_orderedDither);

            pl->addWidget(makeParamLabel("Fusion"));
            m_fusion = new PopupPicker(1);
            m_fusion->setEntries(blendPickerEntries());
            m_fusion->setValue(int(BlendMode::Normal));
            m_fusion->onSelected = [this](QVariant) {
                if (!m_updating && onBlendChanged) onBlendChanged();
            };
            pl->addWidget(m_fusion);
        }
        auto* paramsSec = new PanelSection("Parameters", /*collapsible*/ false, true);
        paramsSec->body()->addWidget(paramsContent);
        vl->addWidget(paramsSec);
    }

    void setAnimatedParams(const QSet<ParamId>& ids)
    {
        m_cellSize->setAnimated(ids.contains(ParamId::AsCellSize));
        m_gamma->setAnimated(ids.contains(ParamId::AsGamma));
        m_edges->setAnimated(ids.contains(ParamId::AsEdges));
        m_hatching->setAnimated(ids.contains(ParamId::AsHatching));
        m_stipple->setAnimated(ids.contains(ParamId::AsStipple));
        m_contour->setAnimated(ids.contains(ParamId::AsContour));
    }

    QHash<QWidget*, ParamId> paramWidgets() const
    {
        return {
            { m_cellSize, ParamId::AsCellSize },
            { m_gamma,    ParamId::AsGamma },
            { m_edges,    ParamId::AsEdges },
            { m_hatching, ParamId::AsHatching },
            { m_stipple,  ParamId::AsStipple },
            { m_contour,  ParamId::AsContour },
        };
    }

    AsciiSettings settings() const
    {
        static const int kWeights[] = { 400, 500, 600, 700 };
        AsciiSettings s;
        s.charsetPreset  = m_charset->value().toInt();
        s.customCharset  = m_customEdit->text();
        s.cellSize       = m_cellSize->value();
        s.gamma          = m_gamma->value() / 100.0f;
        s.fontFamily     = m_font->value().toString();
        s.fontWeight     = kWeights[qBound(0, m_weight->currentIndex(), 3)];
        s.edges          = m_edges->value();
        s.hatching       = m_hatching->value();
        s.stipple        = m_stipple->value();
        s.contour        = m_contour->value();
        s.orderedDither  = m_orderedDither->isChecked();
        return s;
    }

    void setSettings(const AsciiSettings& s)
    {
        m_updating = true;
        m_charset->setValue(qBound(0, s.charsetPreset, int(asciiCharsetPresets().size())));
        m_customEdit->blockSignals(true);
        m_customEdit->setText(s.customCharset);
        m_customEdit->blockSignals(false);
        m_customEdit->setVisible(s.charsetPreset >= int(asciiCharsetPresets().size()));
        m_cellSize->setValue(s.cellSize);
        m_gamma->setValue(qRound(s.gamma * 100));
        m_edges->setValue(s.edges);
        m_hatching->setValue(s.hatching);
        m_stipple->setValue(s.stipple);
        m_contour->setValue(s.contour);
        m_orderedDither->blockSignals(true);
        m_orderedDither->setChecked(s.orderedDither);
        m_orderedDither->blockSignals(false);
        m_font->setValue(s.fontFamily);
        m_weight->blockSignals(true);
        int wi = 2;
        if      (s.fontWeight <= 400) wi = 0;
        else if (s.fontWeight <= 500) wi = 1;
        else if (s.fontWeight <= 600) wi = 2;
        else                          wi = 3;
        m_weight->setCurrentIndex(wi);
        m_weight->blockSignals(false);
        m_updating = false;
    }

    BlendMode blend() const { return BlendMode(m_fusion->value().toInt()); }
    void setBlend(BlendMode m) { m_fusion->setValue(int(m)); }

private:
    void fire() { if (!m_updating && onChanged) onChanged(); }

    PopupPicker*     m_charset    = nullptr;
    QLineEdit*       m_customEdit = nullptr;
    PopupPicker*     m_font       = nullptr;
    NoWheelComboBox* m_weight     = nullptr;
    SliderRow*       m_cellSize   = nullptr;
    SliderRow*       m_gamma      = nullptr;
    SliderRow*       m_edges      = nullptr;
    SliderRow*       m_hatching   = nullptr;
    SliderRow*       m_stipple    = nullptr;
    SliderRow*       m_contour    = nullptr;
    QPushButton*     m_orderedDither = nullptr;
    PopupPicker*     m_fusion     = nullptr;
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
    m_ditherPage->onBlendChanged = [this]() {
        if (!m_updating) emit blendChanged(m_ditherPage->blend());
    };
    m_asciiPage->onBlendChanged = [this]() {
        if (!m_updating) emit blendChanged(m_asciiPage->blend());
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
    m_ditherPage->setBlend(layer.blend);
    m_asciiPage->setBlend(layer.blend);

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

void ModePanel::setAnimatedParams(const QSet<ParamId>& ids)
{
    m_halftonePage->setAnimatedParams(ids);
    m_ditherPage->setAnimatedParams(ids);
    m_asciiPage->setAnimatedParams(ids);
}

QHash<QWidget*, ParamId> ModePanel::paramWidgets() const
{
    // Only the active page's controls are visible/hit-testable; the other
    // two pages' widgets are hidden, so including them is harmless but
    // pointless — return just the one matching the current mode.
    switch (m_mode) {
        case RenderMode::Halftone: return m_halftonePage->paramWidgets();
        case RenderMode::Dither:   return m_ditherPage->paramWidgets();
        case RenderMode::Ascii:    return m_asciiPage->paramWidgets();
    }
    return {};
}
