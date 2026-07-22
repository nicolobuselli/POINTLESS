#include "TonalControlsWidget.h"
#include "Theme.h"
#include "../core/PaletteStore.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QSpacerItem>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QPainter>
#include <QPaintEvent>
#include <QHideEvent>
#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>
#include <QStyle>

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
    std::function<void()> onExtract;
    std::function<void()> onClosed;

    explicit PalettePopup(QWidget* parent = nullptr)
        : QFrame(parent, Qt::Popup)
    {
        setObjectName("palettePopup");
        setAttribute(Qt::WA_StyledBackground, true);

        // Rows live inside a scroll area so the popup stays short and the saved
        // palettes scroll (vertical bar appears only when they overflow).
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(Ui::px(6), Ui::px(6), Ui::px(6), Ui::px(6));
        outer->setSpacing(0);

        m_scroll = new QScrollArea(this);
        m_scroll->setObjectName("palettePopupScroll");
        m_scroll->setWidgetResizable(true);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        auto* body = new QWidget;
        body->setObjectName("palettePopupBody");
        m_layout = new QVBoxLayout(body);
        m_layout->setContentsMargins(0, 0, Ui::px(6), 0);   // gap before the bar
        m_layout->setSpacing(Ui::px(8));                    // breathing room per item
        m_scroll->setWidget(body);
        outer->addWidget(m_scroll);
    }

    void setCurrentName(const QString& n) { m_currentName = n; }

    // Opens to the left of the whole right column (over the canvas), top-
    // aligned with the header — same placement convention as PopupPicker,
    // so long palette names have room to breathe instead of squeezing into
    // the narrow column.
    void showLeftOfColumn(QWidget* anchor)
    {
        m_pendingDelete = -1;
        reload();
        build();
        setFixedWidth(Ui::px(330));
        // Size the scroll to its content (QScrollArea's own sizeHint is tiny,
        // which opened the popup one-row short); cap it, then scroll.
        const int content = m_scroll->widget()->sizeHint().height() + Ui::px(4);
        m_scroll->setFixedHeight(qMin(content, Ui::px(680)));
        adjustSize();

        QWidget* column = anchor;
        while (column->parentWidget() && column->objectName() != "sidePanel")
            column = column->parentWidget();
        const int columnLeftGlobalX = column->mapToGlobal(QPoint(0, 0)).x();

        QScreen* scr = anchor->screen() ? anchor->screen() : QGuiApplication::primaryScreen();
        const QRect a = scr->availableGeometry();
        int x = columnLeftGlobalX - width();
        int y = anchor->mapToGlobal(QPoint(0, 0)).y();
        if (x < a.left() + 6)              x = a.left() + 6;
        if (x + width() > a.right() - 6)   x = a.right() - 6 - width();
        if (y + height() > a.bottom() - 6) y = a.bottom() - 6 - height();
        if (y < a.top() + 6)               y = a.top() + 6;
        move(x, y);
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

        // Top action: extract a palette from the current image.
        {
            auto* row = new QPushButton;
            row->setObjectName("paletteItem");
            row->setCursor(Qt::PointingHandCursor);
            row->setFixedHeight(Ui::px(46));
            auto* hl = new QHBoxLayout(row);
            hl->setContentsMargins(Ui::px(12), 0, Ui::px(10), 0);
            auto* name = new QLabel("Extract from image");
            name->setAttribute(Qt::WA_TransparentForMouseEvents);
            name->setStyleSheet(QString("background:transparent; color:#E3E3E3; font-size:%1px; font-weight:500;").arg(Ui::px(16)));
            hl->addWidget(name);
            hl->addStretch(1);
            connect(row, &QPushButton::clicked, this, [this]() {
                if (onExtract) onExtract();
                hide();
            });
            m_layout->addWidget(row);
        }

        // "Custom" indicator — shown highlighted when the current palette
        // matches none of the saved ones.
        if (m_currentName == "Custom") {
            auto* row = new QPushButton;
            row->setObjectName("paletteItem");
            row->setProperty("selected", true);
            row->setCursor(Qt::PointingHandCursor);
            row->setFixedHeight(Ui::px(46));
            auto* hl = new QHBoxLayout(row);
            hl->setContentsMargins(Ui::px(12), 0, Ui::px(10), 0);
            auto* name = new QLabel("Custom");
            name->setAttribute(Qt::WA_TransparentForMouseEvents);
            name->setStyleSheet(QString("background:transparent; color:#FFFFFF; font-size:%1px; font-weight:500;").arg(Ui::px(16)));
            hl->addWidget(name);
            hl->addStretch(1);
            connect(row, &QPushButton::clicked, this, [this]() { hide(); });
            m_layout->addWidget(row);
        }

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
        const bool selected = (pal.name == m_currentName);

        auto* row = new QPushButton;
        row->setObjectName("paletteItem");
        row->setProperty("selected", selected);
        row->setCursor(Qt::PointingHandCursor);
        row->setFixedHeight(Ui::px(46));

        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(Ui::px(12), 0, Ui::px(8), 0);
        hl->setSpacing(Ui::px(8));

        auto* name = new QLabel(pal.name);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        name->setStyleSheet(QString("background:transparent; color:%1; font-size:%2px; font-weight:500;")
                            .arg(selected ? "#FFFFFF" : "#E3E3E3").arg(Ui::px(16)));

        auto* strip = new SwatchStrip(Ui::px(20));
        strip->setColors(pal.colors);

        auto* trash = makeIconButton(":/icons/trash.svg");
        trash->setObjectName("paletteTrash");   // no box; red tint on hover
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
        row->setFixedHeight(Ui::px(34));

        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(Ui::px(10), 0, Ui::px(6), 0);
        hl->setSpacing(Ui::px(6));

        auto* lbl = new QLabel("Delete?");
        lbl->setStyleSheet("background:transparent; color:#E3E3E3; font-size:9pt;");

        auto* cancel = new QPushButton("Cancel");
        cancel->setObjectName("ghostMini");
        cancel->setCursor(Qt::PointingHandCursor);
        cancel->setFixedHeight(Ui::px(24));

        auto* del = new QPushButton("Delete");
        del->setObjectName("dangerMini");
        del->setCursor(Qt::PointingHandCursor);
        del->setFixedHeight(Ui::px(24));

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

    QScrollArea*               m_scroll = nullptr;
    QVBoxLayout*               m_layout = nullptr;
    std::vector<PalettePreset> m_library;
    int                        m_pendingDelete = -1;
    QString                    m_currentName;   // highlight the active palette
};

// ============================================================
//  SavePalettePopup — floating "Save palette" panel
// ============================================================

class SavePalettePopup : public QFrame
{
public:
    std::function<void(const QString&)> onSave;   // name

    explicit SavePalettePopup(QWidget* parent = nullptr)
        : QFrame(parent, Qt::Popup)
    {
        setObjectName("savePopup");
        setAttribute(Qt::WA_StyledBackground, true);
        setFixedWidth(Ui::px(360));

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(Ui::px(20), Ui::px(16), Ui::px(20), Ui::px(18));
        v->setSpacing(Ui::px(14));

        auto* tr = new QHBoxLayout;
        tr->setContentsMargins(0, 0, 0, 0);
        auto* title = new QLabel("Save palette");
        title->setStyleSheet(QString("background:transparent; color:#EEEEEE; font-size:%1px; font-weight:700;").arg(Ui::px(20)));
        auto* x = new QPushButton(QString::fromUtf8("\xC3\x97"));   // ×
        x->setObjectName("closeMini");
        x->setCursor(Qt::PointingHandCursor);
        x->setFixedSize(Ui::px(32), Ui::px(32));
        x->setStyleSheet(QString("QPushButton#closeMini{background:transparent;border:none;color:#B2B2B2;font-size:%1px;font-weight:600;}"
                                 "QPushButton#closeMini:hover{color:#FFFFFF;}").arg(Ui::px(28)));
        tr->addWidget(title, 1);
        tr->addWidget(x);
        v->addLayout(tr);

        m_strip = new SwatchStrip(Ui::px(24));
        v->addWidget(m_strip);

        m_name = new QLineEdit;
        m_name->setPlaceholderText("Name…");
        v->addWidget(m_name);

        auto* br = new QHBoxLayout;
        br->setContentsMargins(0, 0, 0, 0);
        br->addStretch(1);
        auto* save = new QPushButton("Save");
        save->setObjectName("accentBtn");
        save->setFixedHeight(Ui::px(36));
        save->setMinimumWidth(Ui::px(92));
        save->setStyleSheet(QString("QPushButton#accentBtn{min-height:%1px;padding:0 %2px;font-size:%3px;"
                                     "background-color:#D2FC51;color:#1E1E1E;}"
                                     "QPushButton#accentBtn:hover{background-color:#DFFF7A;}"
                                     "QPushButton#accentBtn:pressed{background-color:#B9DE3F;}")
                                .arg(Ui::px(36)).arg(Ui::px(16)).arg(Ui::px(15)));
        save->setCursor(Qt::PointingHandCursor);
        br->addWidget(save);
        v->addLayout(br);

        connect(x,    &QPushButton::clicked, this, [this]() { hide(); });
        connect(save, &QPushButton::clicked, this, [this]() { commit(); });
        connect(m_name, &QLineEdit::returnPressed, this, [this]() { commit(); });
    }

    void showFor(QWidget* anchor, const std::vector<QColor>& colors)
    {
        m_strip->setColors(colors);
        m_name->clear();
        adjustSize();
        QScreen* scr = anchor->screen() ? anchor->screen() : QGuiApplication::primaryScreen();
        const QRect a = scr->availableGeometry();
        // Centre the popup over the panel that holds the controls (not off to
        // the side): horizontally on the panel centre, vertically by the anchor.
        QWidget* panel = parentWidget() ? parentWidget() : anchor;
        const QRect pg(panel->mapToGlobal(QPoint(0, 0)), panel->size());
        int x = pg.center().x() - width() / 2;
        int y = anchor->mapToGlobal(QPoint(0, anchor->height() + Ui::px(8))).y();
        x = qBound(a.left() + 6, x, a.right() - width() - 6);
        if (y + height() > a.bottom() - 6)
            y = qMax(a.top() + 6, a.bottom() - 6 - height());
        move(x, y);
        show();
        m_name->setFocus();
    }

private:
    void commit()
    {
        const QString n = m_name->text().trimmed();
        if (n.isEmpty()) return;
        if (onSave) onSave(n);
        hide();
    }
    SwatchStrip* m_strip = nullptr;
    QLineEdit*   m_name  = nullptr;
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
    vl->setSpacing(Ui::px(Ui::kGapRows));

    // The body extends to the +/− gutter (24px). The favourite sits in that
    // gutter; every other control stops kGutterComp earlier (the kColRight box
    // gutter), so all box right-edges line up exactly.
    const int kFavW       = 24;           // match iconBtn (toggles) width
    const int kGutterComp = Ui::px(Ui::kColRight - 14);   // kColRight − 14 (Fill body right margin)

    // ── Palette selector + colour count + favourite(save) ───────
    m_paletteSection = new QWidget;
    {
        auto* pl = new QVBoxLayout(m_paletteSection);
        pl->setContentsMargins(0, 0, 0, 0);
        pl->setSpacing(Ui::px(Ui::kGapLabelToCtrl));
        m_paletteLabel = makeParamLabel("Palette");
        pl->addWidget(m_paletteLabel);

        auto* row = new QHBoxLayout;
        m_paletteRow = row;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);   // gaps added explicitly so right-edges line up

        m_paletteHeader = new QPushButton;
        m_paletteHeader->setObjectName("paletteHeader");
        m_paletteHeader->setCursor(Qt::PointingHandCursor);
        m_paletteHeader->setFixedHeight(Ui::px(Ui::kBoxH));
        {
            auto* hl = new QHBoxLayout(m_paletteHeader);
            hl->setContentsMargins(Ui::px(14), 0, Ui::px(10), 0);
            hl->setSpacing(Ui::px(8));
            // Closed header shows the colours only (no name); the active palette
            // name lives in the dropdown highlight instead.
            m_paletteName = new QLabel("Custom", this);   // text storage only
            m_paletteName->hide();
            m_palettePreview = new SwatchStrip(Ui::px(20));
            m_palettePreview->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_paletteChevron = new ChevronButton(ChevronButton::Down);
            m_paletteChevron->setAttribute(Qt::WA_TransparentForMouseEvents);
            hl->addWidget(m_palettePreview);
            hl->addStretch(1);
            hl->addWidget(m_paletteChevron);
        }
        connect(m_paletteHeader, &QPushButton::clicked, this, [this]() { openPalettePopup(); });
        row->addWidget(m_paletteHeader, 1);
        m_leadGap = new QSpacerItem(Ui::px(8), 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        row->addItem(m_leadGap);   // gap between palette and count

        m_modeCombo = new PopupPicker(1);
        {
            QVector<PopupPickerEntry> entries;
            entries.push_back({ 0, "Image colors", QString(), QString() });
            for (int n = 1; n <= kMaxTones; ++n)
                entries.push_back({ n, QString::number(n) + (n == 1 ? " color" : " colors"), QString(), QString() });
            m_modeCombo->setEntries(entries);
        }
        m_modeCombo->setValue(0);
        m_modeCombo->setFixedWidth(Ui::px(150));
        row->addWidget(m_modeCombo);

        // Count box stops at the 70px box-gutter; the favourite sits in the
        // symbol gutter, right-aligned with the section toggles (spacer + fav
        // = kGutterComp).
        m_favGap = new QSpacerItem(kGutterComp - kFavW, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        row->addItem(m_favGap);

        m_favBtn = new QPushButton;
        m_favBtn->setObjectName("favBtn");
        m_favBtn->setCursor(Qt::PointingHandCursor);
        m_favBtn->setFixedSize(kFavW, Ui::px(Ui::kBoxH));
        m_favBtn->setIcon(QIcon(":/icons/favorite.svg"));
        m_favBtn->setIconSize(QSize(Ui::px(17), Ui::px(22)));
        m_favBtn->setToolTip("Save palette");
        connect(m_favBtn, &QPushButton::clicked, this, [this]() { beginSavePalette(); });
        row->addWidget(m_favBtn);

        pl->addLayout(row);
    }
    vl->addWidget(m_paletteSection);

    m_palettePopup = new PalettePopup(this);
    m_palettePopup->onSelect = [this](const std::vector<QColor>& colors, const QString& name) {
        // Sticky: keep palette-match on if it's already active.
        applyPalette(colors, name,
                     m_settings.mode == ToneMode::Palette ? ToneMode::Palette
                                                          : ToneMode::FixedTones);
    };
    m_palettePopup->onExtract = [this]() { extractFromImage(); };
    m_palettePopup->onClosed = [this]() {
        m_paletteChevron->setDirection(ChevronButton::Down);
        m_lastPopupClose = QDateTime::currentMSecsSinceEpoch();
    };

    // ── Generate random (full width) ────────────────────────────
    m_generateBtn = new QPushButton("generate random");
    m_generateBtn->setObjectName("exportBtn");
    m_generateBtn->setFixedHeight(Ui::px(Ui::kBoxH));
    m_generateBtn->setCursor(Qt::PointingHandCursor);
    connect(m_generateBtn, &QPushButton::clicked, this, [this]() { generateRandom(); });
    {
        auto* gw = new QWidget;
        auto* gl = new QHBoxLayout(gw);
        gl->setContentsMargins(0, 0, kGutterComp, 0);
        gl->addWidget(m_generateBtn);
        vl->addWidget(gw);
    }

    // ── Tone rows ───────────────────────────────────────────────
    m_rowsContainer = new QWidget;
    m_rowsLayout = new QVBoxLayout(m_rowsContainer);
    m_rowsLayout->setContentsMargins(0, 0, kGutterComp, 0);
    m_rowsLayout->setSpacing(Ui::px(8));
    vl->addWidget(m_rowsContainer);

    // ── Save palette popup (opened by the favourite button) ──────
    m_savePopup = new SavePalettePopup(this);
    m_savePopup->onSave = [this](const QString& name) {
        PaletteStore::save(name, currentColors());
        reloadLibrary();
        m_paletteName->setText(name);
        refreshPreview();
    };

    // ── Signals ─────────────────────────────────────────────────
    m_modeCombo->onSelected = [this](QVariant v) {
        if (m_updating) return;
        const int idx = v.toInt();
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
    };

    syncModeCombo();
    rebuildRows();
    m_paletteName->setText(matchLibraryName());
    refreshPreview();
}

void TonalControlsWidget::setSettings(const TonalSettings& s)
{
    // applyParams() pushes the active layer's tonal settings back into this
    // panel on almost every edit (any param change, timeline scrub — see
    // MainWindow::applyParams), even when tonal itself didn't change.
    // rebuildRows() below destroys and recreates every swatch/slider, which
    // if the mouse happens to be hovering one (only possible in multi-tone
    // Palette/FixedTones layouts, since a single tone hides its slider)
    // yanks a live widget out from under a native hover/tooltip and flashes
    // a phantom tooltip window. Skip the whole rebuild when nothing changed.
    if (s == m_settings) return;
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
        m_modeCombo->setValue(0);
    else
        m_modeCombo->setValue(qBound(1, int(m_settings.tones.size()), kMaxTones));
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
    const bool imageColors = (m_settings.mode == ToneMode::ImageColors);
    const bool tonesActive = (m_settings.mode == ToneMode::FixedTones) || palette;

    // The colour-count dropdown stays visible in every mode (it's the only way
    // back from Image colors). The palette box / caption / save icon are about
    // *choosing* a palette, which is meaningless in Image colors — hide them
    // there and let the dropdown span the row.
    m_paletteSection->setVisible(true);
    if (m_paletteLabel)  m_paletteLabel->setVisible(!imageColors);
    if (m_paletteHeader) m_paletteHeader->setVisible(!imageColors);
    if (m_favBtn)        m_favBtn->setVisible(!imageColors);
    // With the header + favourite hidden, the combo spans the row. Drop the
    // leading gap and widen the trailing one by the (now absent) favourite width
    // so the combo's right edge still lands on the kColRight gutter.
    const int kFavW = 24, kGutterComp = Ui::px(Ui::kColRight - 14);   // kColRight − 14 (Fill body margin)
    if (imageColors) {
        m_modeCombo->setMinimumWidth(Ui::px(150));
        m_modeCombo->setMaximumWidth(QWIDGETSIZE_MAX);
        m_modeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (m_leadGap) m_leadGap->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        if (m_favGap)  m_favGap->changeSize(kGutterComp, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        // paletteHeader (the row's only other stretch=1 item) is hidden in this
        // mode; a hidden widget still holds its stretch registration, so the
        // combo needs its own explicit stretch to actually claim the row's
        // leftover width instead of just sizing to its minimum.
        if (m_paletteRow) m_paletteRow->setStretchFactor(m_modeCombo, 1);
    } else {
        // Equal width with the palette header (both stretch=1) instead of a
        // fixed 150px — the two boxes now match like every other twin-box row.
        m_modeCombo->setMinimumWidth(Ui::px(150));
        m_modeCombo->setMaximumWidth(QWIDGETSIZE_MAX);
        m_modeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (m_leadGap) m_leadGap->changeSize(Ui::px(Ui::kGapTwinBoxes), 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        if (m_favGap)  m_favGap->changeSize(kGutterComp - kFavW, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        if (m_paletteRow) m_paletteRow->setStretchFactor(m_modeCombo, 1);
    }
    if (m_paletteRow) m_paletteRow->invalidate();
    m_rowsContainer->setVisible(tonesActive);
    if (m_saveSection) m_saveSection->setVisible(tonesActive);
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
        rvl->setSpacing(Ui::px(Ui::kGapLabelToCtrl));
        // Single fixed tone needs no "Color" caption; multi-tone / palette do.
        if (palette || n > 1)
            rvl->addWidget(makeParamLabel(
                palette ? QString("Color %1").arg(i + 1)
                        : (i < labels.size() ? labels[i] : QString("Tone %1").arg(i + 1))));

        auto* hl = new QHBoxLayout;
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(Ui::px(10));

        auto* swatch = new FillSwatch(m_settings.tones[i].color, m_settings.tones[i].opacity, /*showOpacity*/ true);

        auto* slider = new NoWheelSlider(Qt::Horizontal);
        slider->setRange(0, 255);
        slider->setValue(m_settings.tones[i].level);
        slider->setFixedHeight(Ui::px(30));   // match the other sliders
        slider->setVisible(n > 1);            // per-colour threshold only when multi-colour

        if (n > 1) {
            swatch->setFixedWidth(Ui::px(185));
            hl->addWidget(swatch);
            hl->addWidget(slider, 1);
        } else {
            // Single colour: full-width swatch, like the Background fill box.
            hl->addWidget(swatch, 1);
            hl->addWidget(slider);   // hidden, takes no space
        }
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
    m_palettePopup->setCurrentName(matchLibraryName());
    m_palettePopup->showLeftOfColumn(m_paletteHeader);
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
    if (m_savePopup) m_savePopup->showFor(m_paletteHeader, currentColors());
}

void TonalControlsWidget::commitSavePalette()
{
    // Saving is handled by the popup's onSave callback.
}

void TonalControlsWidget::cancelSavePalette()
{
    if (m_savePopup) m_savePopup->hide();
}

void TonalControlsWidget::generateRandom()
{
    // Use the count chosen in the shared dropdown (fall back to a small
    // palette when "Image colors" is selected).
    int n = m_modeCombo->value().toInt();   // 0 = Image colors, else N
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
