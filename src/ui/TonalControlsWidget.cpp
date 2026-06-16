#include "TonalControlsWidget.h"
#include "../core/PaletteStore.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QPainter>
#include <QPaintEvent>
#include <QHideEvent>
#include <QDateTime>

namespace {
constexpr int kMaxTones = 8;
} // namespace

// ============================================================
//  SwatchStrip — a compact preview of a palette's colors
// ============================================================

class SwatchStrip : public QWidget
{
public:
    explicit SwatchStrip(int diameter = 18, QWidget* parent = nullptr)
        : QWidget(parent), m_d(diameter)
    {
        setFixedHeight(m_d);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setStyleSheet("background: transparent;");   // override global QWidget bg
    }

    void setColors(const std::vector<QColor>& colors)
    {
        m_colors = colors;
        const int step = m_d - 6;
        const int n    = int(m_colors.size());
        setFixedWidth(n > 0 ? m_d + step * (n - 1) + 1 : 0);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_colors.empty()) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int step = m_d - 6;
        float x = 0.5f;
        for (const QColor& c : m_colors) {
            p.setPen(QPen(QColor("#272727"), 1.0));
            p.setBrush(c);
            p.drawEllipse(QRectF(x, 0.5f, m_d - 1.0f, m_d - 1.0f));
            x += step;
        }
    }

private:
    int                 m_d;
    std::vector<QColor> m_colors;
};

// ============================================================
//  PalettePopup — floating dropdown over the controls below.
//
//  Lists the saved palette library; selecting a row loads it.
//  The trash button turns that row into an inline confirm strip
//  (red Delete / Cancel) so no nested popup is needed.
// ============================================================

class PalettePopup : public QFrame
{
public:
    std::function<void(const std::vector<QColor>&, const QString&)> onSelect;
    std::function<void()> onClosed;

    explicit PalettePopup(QWidget* parent = nullptr)
        : QFrame(parent, Qt::Popup)
    {
        setObjectName("palettePopup");
        setAttribute(Qt::WA_StyledBackground, true);
        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(6, 6, 6, 6);
        m_layout->setSpacing(4);
    }

    void showBelow(QWidget* anchor)
    {
        m_pendingDelete = -1;
        reload();
        build();
        setFixedWidth(anchor->width());
        move(anchor->mapToGlobal(QPoint(0, anchor->height() + 4)));
        show();
    }

protected:
    void hideEvent(QHideEvent* e) override
    {
        QFrame::hideEvent(e);
        if (onClosed) onClosed();
    }

private:
    void reload() { m_library = PaletteStore::all(); }

    void clear()
    {
        while (QLayoutItem* it = m_layout->takeAt(0)) {
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
    }

    void build()
    {
        clear();
        if (m_library.empty()) {
            auto* empty = new QLabel("No saved palettes");
            empty->setStyleSheet("color:#808080; background:transparent;"
                                 " font-size:9pt; padding:6px;");
            m_layout->addWidget(empty);
        } else {
            for (int i = 0; i < int(m_library.size()); ++i)
                m_layout->addWidget(i == m_pendingDelete ? buildConfirmRow(i)
                                                         : buildItemRow(i));
        }
        adjustSize();
    }

    QWidget* buildItemRow(int i)
    {
        const PalettePreset& pal = m_library[i];

        auto* row = new QPushButton;
        row->setObjectName("paletteItem");
        row->setCursor(Qt::PointingHandCursor);
        row->setFixedHeight(34);

        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(12, 0, 6, 0);
        hl->setSpacing(8);

        auto* name = new QLabel(pal.name);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        name->setStyleSheet("background:transparent; color:#E3E3E3; font-size:9pt;");

        auto* strip = new SwatchStrip(16);
        strip->setColors(pal.colors);

        auto* trash = makeIconButton(":/icons/trash.svg");
        trash->setObjectName("trashBtn");
        trash->setCursor(Qt::PointingHandCursor);

        hl->addWidget(name);
        hl->addStretch(1);
        hl->addWidget(strip);
        hl->addWidget(trash);

        const std::vector<QColor> colors = pal.colors;
        const QString             nm     = pal.name;
        connect(row, &QPushButton::clicked, this, [this, colors, nm]() {
            if (onSelect) onSelect(colors, nm);
            hide();
        });
        connect(trash, &QPushButton::clicked, this, [this, i]() {
            m_pendingDelete = i;
            build();
        });
        return row;
    }

    QWidget* buildConfirmRow(int i)
    {
        auto* row = new QFrame;
        row->setObjectName("paletteConfirm");
        row->setAttribute(Qt::WA_StyledBackground, true);
        row->setFixedHeight(34);

        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(10, 0, 6, 0);
        hl->setSpacing(6);

        auto* lbl = new QLabel("Delete?");
        lbl->setStyleSheet("background:transparent; color:#E3E3E3; font-size:9pt;");

        auto* cancel = new QPushButton("Cancel");
        cancel->setObjectName("ghostMini");
        cancel->setCursor(Qt::PointingHandCursor);
        cancel->setFixedHeight(24);

        auto* del = new QPushButton("Delete");
        del->setObjectName("dangerMini");
        del->setCursor(Qt::PointingHandCursor);
        del->setFixedHeight(24);

        hl->addWidget(lbl);
        hl->addStretch(1);
        hl->addWidget(cancel);
        hl->addWidget(del);

        connect(cancel, &QPushButton::clicked, this, [this]() {
            m_pendingDelete = -1;
            build();
        });
        connect(del, &QPushButton::clicked, this, [this, i]() {
            PaletteStore::remove(i);
            m_pendingDelete = -1;
            reload();
            build();
        });
        return row;
    }

    QVBoxLayout*               m_layout = nullptr;
    std::vector<PalettePreset> m_library;
    int                        m_pendingDelete = -1;
};

// ============================================================
//  TonalControlsWidget
// ============================================================

TonalControlsWidget::TonalControlsWidget(const TonalSettings& initial, QWidget* parent)
    : QWidget(parent)
    , m_settings(initial)
{
    if (m_settings.tones.empty())
        m_settings.tones = defaultTones(1);

    reloadLibrary();

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    // ── Palette selector (header opens a floating popup) ────────
    m_paletteSection = new QWidget;
    {
        auto* pl = new QVBoxLayout(m_paletteSection);
        pl->setContentsMargins(0, 0, 0, 0);
        pl->setSpacing(4);
        pl->addWidget(makeParamLabel("Palette"));

        m_paletteHeader = new QPushButton;
        m_paletteHeader->setObjectName("paletteHeader");
        m_paletteHeader->setCursor(Qt::PointingHandCursor);
        m_paletteHeader->setFixedHeight(38);
        {
            auto* hl = new QHBoxLayout(m_paletteHeader);
            hl->setContentsMargins(12, 0, 8, 0);
            hl->setSpacing(8);
            m_paletteName = new QLabel("Custom");
            m_paletteName->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_paletteName->setStyleSheet("background:transparent; color:#E3E3E3; font-size:10pt;");
            m_palettePreview = new SwatchStrip(18);
            m_paletteChevron = new ChevronButton(ChevronButton::Down);
            m_paletteChevron->setAttribute(Qt::WA_TransparentForMouseEvents);
            hl->addWidget(m_paletteName);
            hl->addStretch(1);
            hl->addWidget(m_palettePreview);
            hl->addWidget(m_paletteChevron);
        }
        connect(m_paletteHeader, &QPushButton::clicked, this, [this]() { openPalettePopup(); });
        pl->addWidget(m_paletteHeader);
    }
    vl->addWidget(m_paletteSection);

    m_palettePopup = new PalettePopup(this);
    m_palettePopup->onSelect = [this](const std::vector<QColor>& colors, const QString& name) {
        // Sticky: keep palette-match on if it's already active.
        applyPalette(colors, name,
                     m_settings.mode == ToneMode::Palette ? ToneMode::Palette
                                                          : ToneMode::FixedTones);
    };
    m_palettePopup->onClosed = [this]() {
        m_paletteChevron->setDirection(ChevronButton::Down);
        m_lastPopupClose = QDateTime::currentMSecsSinceEpoch();
    };

    // ── Color count + Generate random (shared row) ──────────────
    {
        auto* countRow = new QWidget;
        auto* cl = new QHBoxLayout(countRow);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(8);

        m_modeCombo = new NoWheelComboBox;
        m_modeCombo->addItem("Image colors");
        for (int n = 1; n <= kMaxTones; ++n)
            m_modeCombo->addItem(QString::number(n) + (n == 1 ? " color" : " colors"));
        cl->addWidget(m_modeCombo, 1);

        m_generateBtn = new QPushButton("Generate\nrandom");
        m_generateBtn->setObjectName("exportBtn");
        m_generateBtn->setCursor(Qt::PointingHandCursor);
        connect(m_generateBtn, &QPushButton::clicked, this, [this]() { generateRandom(); });
        cl->addWidget(m_generateBtn, 1);

        vl->addWidget(countRow);
    }

    // ── Extract from image (full width; auto-enables palette dither) ──
    m_extraRow = new QWidget;
    {
        auto* xl = new QHBoxLayout(m_extraRow);
        xl->setContentsMargins(0, 0, 0, 0);
        xl->setSpacing(0);

        m_extractBtn = new QPushButton("Extract from image");
        m_extractBtn->setObjectName("exportBtn");
        m_extractBtn->setCursor(Qt::PointingHandCursor);
        m_extractBtn->setToolTip("Pull an 8-colour palette from the image and dither to it");
        connect(m_extractBtn, &QPushButton::clicked, this, [this]() { extractFromImage(); });
        xl->addWidget(m_extractBtn);
    }
    vl->addWidget(m_extraRow);

    // ── Tone rows ───────────────────────────────────────────────
    m_rowsContainer = new QWidget;
    m_rowsLayout = new QVBoxLayout(m_rowsContainer);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(8);
    vl->addWidget(m_rowsContainer);

    // ── Save palette ────────────────────────────────────────────
    m_saveSection = new QWidget;
    {
        auto* sl = new QVBoxLayout(m_saveSection);
        sl->setContentsMargins(0, 0, 0, 0);
        sl->setSpacing(0);

        m_saveBtn = new QPushButton("Save palette");
        m_saveBtn->setObjectName("exportBtn");
        m_saveBtn->setFixedHeight(42);
        m_saveBtn->setCursor(Qt::PointingHandCursor);
        connect(m_saveBtn, &QPushButton::clicked, this, [this]() { beginSavePalette(); });
        sl->addWidget(m_saveBtn);

        m_saveEditRow = new QWidget;
        m_saveEditRow->setObjectName("saveEditBox");
        {
            auto* el = new QVBoxLayout(m_saveEditRow);
            el->setContentsMargins(10, 10, 10, 10);
            el->setSpacing(8);
            m_saveNameEdit = new QLineEdit;
            m_saveNameEdit->setPlaceholderText("Save as…");
            el->addWidget(m_saveNameEdit);
            auto* btnRow = new QHBoxLayout;
            btnRow->setContentsMargins(0, 0, 0, 0);
            btnRow->addStretch(1);
            auto* confirm = new QPushButton("Confirm");
            confirm->setObjectName("accentBtn");
            confirm->setFixedHeight(34);
            confirm->setCursor(Qt::PointingHandCursor);
            connect(confirm, &QPushButton::clicked, this, [this]() { commitSavePalette(); });
            connect(m_saveNameEdit, &QLineEdit::returnPressed, this, [this]() { commitSavePalette(); });
            btnRow->addWidget(confirm);
            el->addLayout(btnRow);
        }
        m_saveEditRow->setVisible(false);
        sl->addWidget(m_saveEditRow);
    }
    vl->addWidget(m_saveSection);

    // ── Signals ─────────────────────────────────────────────────
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (m_updating) return;
        if (idx == 0) {
            m_settings.mode = ToneMode::ImageColors;
        } else {
            // Picking a count is the explicit way back to luminosity tones.
            m_settings.mode = ToneMode::FixedTones;
            const int n = idx;
            // Preserve existing colors, re-space the levels evenly.
            std::vector<ToneEntry> fresh = defaultTones(n);
            for (int i = 0; i < n && i < int(m_settings.tones.size()); ++i)
                fresh[i].color = m_settings.tones[i].color;
            m_settings.tones = fresh;
        }
        rebuildRows();
        markCustom();
        emitChanged();
    });

    syncModeCombo();
    rebuildRows();
    m_paletteName->setText(matchLibraryName());
    refreshPreview();
}

void TonalControlsWidget::setSettings(const TonalSettings& s)
{
    m_settings = s;
    if (m_settings.tones.empty())
        m_settings.tones = defaultTones(1);
    if (int(m_settings.tones.size()) > kMaxTones)
        m_settings.tones.resize(kMaxTones);

    m_updating = true;
    syncModeCombo();
    m_updating = false;

    if (m_palettePopup && m_palettePopup->isVisible()) m_palettePopup->hide();
    cancelSavePalette();
    rebuildRows();
    m_paletteName->setText(matchLibraryName());
    refreshPreview();
}

void TonalControlsWidget::syncModeCombo()
{
    const bool prev = m_updating;
    m_updating = true;
    if (m_settings.mode == ToneMode::ImageColors)
        m_modeCombo->setCurrentIndex(0);
    else
        m_modeCombo->setCurrentIndex(qBound(1, int(m_settings.tones.size()), kMaxTones));
    m_updating = prev;
}

QStringList TonalControlsWidget::labelsFor(int n)
{
    switch (n) {
        case 1:  return { "Color" };
        case 2:  return { "Highlights", "Shadows" };
        case 3:  return { "Highlights", "Midtones", "Shadows" };
        case 4:  return { "Highlights", "Light midtones", "Dark midtones", "Shadows" };
        default: return { "Highlights", "Light midtones", "Midtones", "Dark midtones", "Shadows" };
    }
}

void TonalControlsWidget::rebuildRows()
{
    m_rows.clear();
    while (QLayoutItem* item = m_rowsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    const bool palette     = (m_settings.mode == ToneMode::Palette);
    const bool tonesActive = (m_settings.mode == ToneMode::FixedTones) || palette;
    m_paletteSection->setVisible(tonesActive);
    m_rowsContainer->setVisible(tonesActive);
    m_saveSection->setVisible(tonesActive);
    if (m_generateBtn) m_generateBtn->setVisible(tonesActive);
    if (m_extraRow)    m_extraRow->setVisible(tonesActive);
    cancelSavePalette();
    if (!tonesActive) return;

    const int n = int(m_settings.tones.size());
    const QStringList labels = labelsFor(n);

    for (int i = 0; i < n; ++i) {
        auto* rowWidget = new QWidget;
        auto* rvl = new QVBoxLayout(rowWidget);
        rvl->setContentsMargins(0, 0, 0, 0);
        rvl->setSpacing(4);
        rvl->addWidget(makeParamLabel(
            palette ? QString("Color %1").arg(i + 1)
                    : (i < labels.size() ? labels[i] : QString("Tone %1").arg(i + 1))));

        auto* hl = new QHBoxLayout;
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(10);

        auto* swatch = new FillSwatch(m_settings.tones[i].color, m_settings.tones[i].opacity, /*showOpacity*/ true);
        swatch->setFixedWidth(160);

        auto* slider = new NoWheelSlider(Qt::Horizontal);
        slider->setRange(0, 255);
        slider->setValue(m_settings.tones[i].level);
        slider->setVisible(!palette);   // level anchors are irrelevant for palette match

        hl->addWidget(swatch);
        hl->addWidget(slider, 1);
        rvl->addLayout(hl);

        m_rowsLayout->addWidget(rowWidget);
        m_rows.push_back({ swatch, slider });

        swatch->onClicked = [this, i, swatch]() { openTonePicker(i, swatch); };
        swatch->onColorEdited = [this, i](QColor c) {
            if (i >= int(m_settings.tones.size())) return;
            m_settings.tones[i].color = c;
            markCustom();
            emitChanged();
        };
        connect(slider, &QSlider::valueChanged, this, [this, i](int v) {
            if (m_updating) return;
            if (i < int(m_settings.tones.size())) {
                m_settings.tones[i].level = v;
                markCustom();
                emitChanged();
            }
        });
    }
}

void TonalControlsWidget::openTonePicker(int idx, QWidget* anchor)
{
    if (idx < 0 || idx >= int(m_settings.tones.size())) return;
    auto* dlg = new ColorPickerDialog(m_settings.tones[idx].color,
                                      m_settings.tones[idx].opacity, /*showOpacity*/ true, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->moveNextTo(anchor);

    dlg->onColorChanged = [this, idx](QColor c, float opacity) {
        if (idx >= int(m_settings.tones.size())) return;
        m_settings.tones[idx].color = c;
        m_settings.tones[idx].opacity = opacity;
        if (idx < int(m_rows.size())) {
            m_rows[idx].swatch->setColor(c);
            m_rows[idx].swatch->setOpacity(opacity);
        }
        markCustom();
        emitChanged();
    };

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void TonalControlsWidget::emitChanged()
{
    if (!m_updating && onChanged) onChanged();
}

// ── Palette selector ─────────────────────────────────────────

void TonalControlsWidget::reloadLibrary()
{
    m_library = PaletteStore::all();
}

void TonalControlsWidget::openPalettePopup()
{
    // Guard against the outside-click that just dismissed the popup
    // landing on the header and immediately reopening it.
    if (QDateTime::currentMSecsSinceEpoch() - m_lastPopupClose < 200) return;
    m_paletteChevron->setDirection(ChevronButton::Up);
    m_palettePopup->showBelow(m_paletteHeader);
}

void TonalControlsWidget::applyPalette(const std::vector<QColor>& colors, const QString& name,
                                       ToneMode mode)
{
    if (colors.empty()) return;
    m_settings.mode  = mode;
    auto tones = tonesFromColors(colors);
    if (int(tones.size()) > kMaxTones) tones.resize(kMaxTones);
    m_settings.tones = tones;

    m_updating = true;
    syncModeCombo();
    m_updating = false;

    cancelSavePalette();
    rebuildRows();
    m_paletteName->setText(name);
    refreshPreview();
    emitChanged();
}

void TonalControlsWidget::markCustom()
{
    if (m_paletteName) m_paletteName->setText("Custom");
    refreshPreview();
}

void TonalControlsWidget::refreshPreview()
{
    if (m_palettePreview) m_palettePreview->setColors(currentColors());
}

std::vector<QColor> TonalControlsWidget::currentColors() const
{
    std::vector<QColor> out;
    out.reserve(m_settings.tones.size());
    for (const auto& t : m_settings.tones) out.push_back(t.color);
    return out;
}

QString TonalControlsWidget::matchLibraryName() const
{
    const std::vector<QColor> cur = currentColors();
    for (const PalettePreset& pal : m_library) {
        if (pal.colors.size() != cur.size()) continue;
        bool same = true;
        for (size_t i = 0; i < cur.size(); ++i)
            if (pal.colors[i].rgb() != cur[i].rgb()) { same = false; break; }
        if (same) return pal.name;
    }
    return "Custom";
}

// ── Save / random ────────────────────────────────────────────

void TonalControlsWidget::beginSavePalette()
{
    m_saveBtn->setVisible(false);
    m_saveEditRow->setVisible(true);
    m_saveNameEdit->clear();
    m_saveNameEdit->setFocus();
}

void TonalControlsWidget::commitSavePalette()
{
    const QString name = m_saveNameEdit->text().trimmed();
    if (name.isEmpty()) { cancelSavePalette(); return; }

    PaletteStore::save(name, currentColors());
    reloadLibrary();
    m_paletteName->setText(name);
    cancelSavePalette();
}

void TonalControlsWidget::cancelSavePalette()
{
    if (m_saveEditRow) m_saveEditRow->setVisible(false);
    if (m_saveBtn)     m_saveBtn->setVisible(true);
}

void TonalControlsWidget::generateRandom()
{
    // Use the count chosen in the shared dropdown (fall back to a small
    // palette when "Image colors" is selected).
    int n = m_modeCombo->currentIndex();   // 0 = Image colors, else N
    if (n < 1) n = 3;
    // Sticky: keep palette-match on if it's already active.
    applyPalette(PaletteStore::randomColors(n), "Custom",
                 m_settings.mode == ToneMode::Palette ? ToneMode::Palette
                                                      : ToneMode::FixedTones);
}

void TonalControlsWidget::extractFromImage()
{
    if (m_sourceImage.isNull()) return;
    // Always pull a full 8-colour palette and dither to it (palette match).
    const auto colors = PaletteStore::fromImage(m_sourceImage, 8);
    if (colors.empty()) return;
    applyPalette(colors, "Custom", ToneMode::Palette);
}
