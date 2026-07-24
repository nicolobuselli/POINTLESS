#include "MainWindow.h"
#include "PreviewWidget.h"
#include "ControlsPanel.h"
#include "UiScale.h"
#include "Theme.h"
#include "ModePanel.h"
#include "FilmstripWidget.h"
#include "TimelineWidget.h"
#include "LayersPanel.h"
#include "Widgets.h"
#include "../workers/RenderWorker.h"
#include "GpuCanvasWidget.h"
#include "../core/ImageAdjuster.h"
#include "../core/ProjectIO.h"
#include "../core/VideoIO.h"
#include "../core/DotGridRenderer.h"
#include "../core/HalftoneRenderer.h"
#include "../core/DitherRenderer.h"
#include "../core/MosaicRenderer.h"
#include "../core/AsciiRenderer.h"

#include <QApplication>
#include <QCursor>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QTransform>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
constexpr int kMaxUndoSteps   = 100;
constexpr int kUndoDebounceMs = 400;

// The whole-layer-mask LocParam for a layer kind — the single "Localize"
// button in Adjustments toggles this one point (position/radius/falloff),
// which the shared LocMask/spotlight logic already masks the render with.
LocParam maskParamFor(LayerKind kind)
{
    switch (kind) {
        case LayerKind::Dither: return LocParam::DiMask;
        case LayerKind::Ascii:  return LocParam::AsMask;
        case LayerKind::Mosaic: return LocParam::MsMask;
        default:                return LocParam::DgMask;
    }
}

bool isVideoFile(const QString& path)
{
    static const QStringList vids = { "mp4", "mov", "avi", "mkv", "webm",
                                      "m4v", "wmv", "mpg", "mpeg", "gif" };
    return vids.contains(QFileInfo(path).suffix().toLower());
}

// True if the selection looks like a numbered image sequence (frame_0001.png,
// frame_0002.png, …): >=3 still images whose names share one stem once the
// trailing digits are stripped. ponytail: stem heuristic, tighten if it
// misfires on oddly-named batches.
bool looksLikeSequence(const QStringList& paths)
{
    if (paths.size() < 3) return false;
    QString stem;
    for (const QString& p : paths) {
        if (isVideoFile(p)) return false;
        QString base = QFileInfo(p).completeBaseName();
        while (!base.isEmpty() && base.back().isDigit()) base.chop(1);
        if (base.isEmpty()) return false;             // pure-number names: ambiguous
        if (stem.isEmpty()) stem = base;
        else if (base != stem) return false;
    }
    return true;
}

QImage placeOnFramePreview(const QImage& layerImg, const LayerTransform& tf, QSize frame)
{
    QImage placed(frame, QImage::Format_ARGB32_Premultiplied);
    placed.fill(Qt::transparent);
    if (layerImg.isNull() || frame.isEmpty()) return placed;

    QPainter p(&placed);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double s  = qMax(0.0001, double(tf.scalePct) / 100.0);
    const double cx = frame.width()  * 0.5 + double(tf.xPct) * frame.width();
    const double cy = frame.height() * 0.5 + double(tf.yPct) * frame.height();

    QTransform m;
    m.translate(cx, cy);
    m.rotate(tf.rotation);
    m.scale(s * (tf.flipH ? -1.0 : 1.0), s * (tf.flipV ? -1.0 : 1.0));
    m.translate(-layerImg.width() * 0.5, -layerImg.height() * 0.5);
    p.setTransform(m);
    p.drawImage(0, 0, layerImg);
    return placed;
}
} // namespace

// ============================================================
//  WinTitleBar — client-drawn caption: logo (left) + window controls (right)
// ============================================================
//
// The window stays a normal native window; MainWindow::nativeEvent strips the OS
// caption (WM_NCCALCSIZE) and re-supplies resize borders + a draggable caption
// region (WM_NCHITTEST → HTCAPTION over this bar, except its buttons). So drag,
// double-click-maximize, Aero Snap and resize all stay native — this widget only
// paints and provides the min/max/close buttons.
class WinTitleBar : public QWidget
{
public:
    explicit WinTitleBar(QWidget* window)
        : QWidget(window), m_window(window)
    {
        setObjectName("windowTitleBar");
        setAttribute(Qt::WA_StyledBackground, true);
        const int barH = Ui::px(44);
        setFixedHeight(barH);

        auto* hl = new QHBoxLayout(this);
        // Left gutter 40 = left column gutter. Right = 0: close button sits
        // flush against the window's right edge.
        hl->setContentsMargins(Ui::px(Ui::kColLeft), 0, 0, 0);
        hl->setSpacing(0);

        const int lh = Ui::px(34);
        const int lw = qRound(lh * (827.0 / 337.0));   // logo_wordmark.svg aspect ratio
        auto* logo = new QLabel;
        logo->setObjectName("titleLogo");
        logo->setPixmap(QIcon(":/logo_wordmark.svg").pixmap(QSize(lw, lh)));
        logo->setAttribute(Qt::WA_TransparentForMouseEvents);  // drag through it
        hl->addWidget(logo);
        hl->addStretch(1);

        auto mkBtn = [&](const QString& icon, const char* obj) {
            auto* b = new QPushButton;
            b->setObjectName(obj);
            b->setCursor(Qt::PointingHandCursor);
            b->setFixedSize(Ui::px(48), barH);   // full bar height: hover/press fill top-to-bottom
            b->setIcon(QIcon(icon));
            b->setIconSize(QSize(Ui::px(20), Ui::px(20)));
            return b;
        };
        m_min = mkBtn(":/icons/win_minimize.svg", "winBtn");
        m_max = mkBtn(":/icons/win_maximize.svg", "winBtn");
        auto* close = mkBtn(":/icons/x.svg", "winCloseBtn");
        hl->addWidget(m_min);
        hl->addSpacing(Ui::px(6));
        hl->addWidget(m_max);
        hl->addSpacing(Ui::px(6));
        hl->addWidget(close);

        connect(m_min, &QPushButton::clicked, m_window, &QWidget::showMinimized);
        connect(m_max, &QPushButton::clicked, this, [this] {
            if (m_window->isMaximized()) m_window->showNormal();
            else                         m_window->showMaximized();
        });
        connect(close, &QPushButton::clicked, m_window, &QWidget::close);
    }

    // Swap maximize ⇄ restore glyph to mirror the current window state.
    void updateMaxIcon()
    {
        m_max->setIcon(QIcon(m_window->isMaximized() ? ":/icons/win_restore.svg"
                                                     : ":/icons/win_maximize.svg"));
    }

private:
    QWidget*     m_window;
    QPushButton* m_min = nullptr;
    QPushButton* m_max = nullptr;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_worker(new RenderWorker(this))
{
    setWindowTitle("POINTLESS");
    setWindowIcon(QIcon(":/logo.png"));
    setMinimumSize(1100, 680);

    // ── Layout ───────────────────────────────────────────────
    auto* central = new QWidget;
    auto* rootV = new QVBoxLayout(central);
    rootV->setContentsMargins(0, 0, 0, 0);
    rootV->setSpacing(0);

    m_titleBar = new WinTitleBar(this);
    rootV->addWidget(m_titleBar);

    auto* content = new QWidget;
    auto* hl = new QHBoxLayout(content);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);

    m_left = new ControlsPanel;

    m_preview   = new PreviewWidget;
    m_filmstrip = new FilmstripWidget;
    m_timeline  = new TimelineWidget;
    m_preview->setMinimumHeight(160);   // keep panes from collapsing to nothing

    // Bottom panel: "Timeline | Library" tabs over a stacked area. Dragging the
    // splitter handle down past the tab row collapses it to just the two
    // titles; dragging back up, or clicking a title, re-expands it.
    auto* bottomPanel = new QWidget;
    bottomPanel->setObjectName("bottomBar");
    auto* bp = new QVBoxLayout(bottomPanel);
    bp->setContentsMargins(0, 0, 0, 0);
    bp->setSpacing(0);

    auto* topLine = new QFrame;
    topLine->setObjectName("bandLine");
    topLine->setFixedHeight(1);
    bp->addWidget(topLine);

    auto* tabRow = new QWidget;
    tabRow->setObjectName("bottomTabRow");
    auto* trl = new QHBoxLayout(tabRow);
    trl->setContentsMargins(Ui::px(16), Ui::px(6), Ui::px(16), Ui::px(6));
    trl->setSpacing(Ui::px(6));
    auto* tabTimeline = new QPushButton("Timeline");
    auto* tabLibrary  = new QPushButton("Library");
    for (QPushButton* b : { tabTimeline, tabLibrary }) {
        b->setObjectName("rectTab");
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setCursor(Qt::PointingHandCursor);
    }
    trl->addWidget(tabLibrary);
    trl->addWidget(tabTimeline);
    trl->addStretch(1);
    tabLibrary->setChecked(true);

    auto* bottomStack = new QStackedWidget;
    bottomStack->addWidget(m_filmstrip);   // page 0 (default, Library)
    bottomStack->addWidget(m_timeline);    // page 1 (Timeline)
    bottomStack->setCurrentIndex(0);   // default to Library

    auto* underTabs = new QFrame;
    underTabs->setObjectName("bandLine");
    underTabs->setFixedHeight(1);

    bp->addWidget(tabRow);
    bp->addWidget(underTabs);
    bp->addWidget(bottomStack, 1);

    // Center column: preview over the bottom panel, vertically resizable.
    auto* centerSplit = new QSplitter(Qt::Vertical);
    centerSplit->setChildrenCollapsible(false);
    centerSplit->setHandleWidth(1);
    centerSplit->addWidget(m_preview);
    centerSplit->addWidget(bottomPanel);
    centerSplit->setStretchFactor(0, 1);
    centerSplit->setStretchFactor(1, 0);
    centerSplit->setSizes({ 600, m_bottomExpandedH });

    // collapsedH = tab row + its two 1px band lines, nothing else. Use
    // minimumHeight() (set by setFixedHeight above), not height(): the widgets
    // aren't shown/laid out yet at this point in the constructor, so height()
    // would still read a stale/default size. This stays the panel's minimum
    // height permanently — it's the collapse floor, never raised again — so
    // the splitter can always be dragged back down to it.
    const int collapsedH = topLine->minimumHeight() + tabRow->sizeHint().height() + underTabs->minimumHeight();
    bottomPanel->setMinimumHeight(collapsedH);

    // Sets only the visual/logical collapsed state (stack visibility + which
    // tab, if any, looks checked). Deliberately does NOT touch centerSplit's
    // sizes: called from splitterMoved mid-drag, forcing a size there would
    // fight the live drag and desync QSplitterHandle's internal tracking.
    // Qt's own minimumHeight floor (set once, above) already stops the drag
    // from going below collapsedH — no manual snapping needed.
    auto setBottomCollapsed = [this, bottomStack, tabTimeline, tabLibrary](bool collapsed, int page) {
        m_bottomCollapsed = collapsed;
        if (!collapsed) m_bottomLastPage = page;
        bottomStack->setVisible(!collapsed);
        // autoExclusive refuses to uncheck the last checked button in the
        // group (see ModePanel::setFromLayer for the same workaround), so
        // drop it momentarily to actually clear both when collapsing.
        for (QPushButton* b : { tabTimeline, tabLibrary }) {
            b->setAutoExclusive(false);
            b->setChecked(false);
            b->setAutoExclusive(true);
        }
        if (!collapsed) {
            (page == 0 ? tabLibrary : tabTimeline)->setChecked(true);
            bottomStack->setCurrentIndex(page);
        }
    };

    // Clicking a title is not a drag: force-grow from collapsed to the
    // original/default height here, since there's no ongoing drag for the
    // floor alone to relax.
    auto onTabClicked = [this, centerSplit, collapsedH, setBottomCollapsed](int page) {
        const bool wasCollapsed = m_bottomCollapsed;
        setBottomCollapsed(false, page);
        if (wasCollapsed) {
            QList<int> sizes = centerSplit->sizes();
            if (sizes.size() == 2) {
                sizes[0] -= (m_bottomExpandedH - sizes[1]);
                sizes[1] = m_bottomExpandedH;
                centerSplit->setSizes(sizes);
            }
        }
    };
    connect(tabLibrary,  &QPushButton::clicked, this, [onTabClicked] { onTabClicked(0); });
    connect(tabTimeline, &QPushButton::clicked, this, [onTabClicked] { onTabClicked(1); });
    connect(centerSplit, &QSplitter::splitterMoved, this,
            [this, bottomPanel, collapsedH, setBottomCollapsed](int, int) {
        const int h = bottomPanel->height();
        if (!m_bottomCollapsed && h <= collapsedH + 4) {
            setBottomCollapsed(true, m_bottomLastPage);
        } else if (m_bottomCollapsed && h > collapsedH + 4) {
            setBottomCollapsed(false, m_bottomLastPage);
        }
    });

    // Layers now live embedded at the top of the left column.
    m_layersPanel = m_left->layers();
    m_right = new ModePanel;

    // Main columns: left | center | right, horizontally resizable.
    auto* mainSplit = new QSplitter(Qt::Horizontal);
    mainSplit->setChildrenCollapsible(false);
    mainSplit->setHandleWidth(1);
    mainSplit->addWidget(m_left);
    mainSplit->addWidget(centerSplit);
    mainSplit->addWidget(m_right);
    mainSplit->setStretchFactor(0, 0);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setStretchFactor(2, 0);
    // Start with the side columns at their minimum width (the user can widen).
    mainSplit->setSizes({ Ui::px(410), Ui::px(1738), Ui::px(410) });

    hl->addWidget(mainSplit);
    rootV->addWidget(content, 1);

    setCentralWidget(central);

    qApp->installEventFilter(this);

    // ── Signals ──────────────────────────────────────────────
    connect(m_left,  &ControlsPanel::adjustmentsChanged, this, &MainWindow::onParamsChanged);
    connect(m_left,  &ControlsPanel::resetRequested,     this, [this]() {
        if (m_current < 0) return;
        if (Layer* l = activeLayer()) {
            l->adjustments = Adjustments{};
            applyParams(m_images[m_current].state);
            syncLayersPanel();
            scheduleRender();
            m_undoTimer.start();
        }
    });
    connect(m_left, &ControlsPanel::fileRenamed, this, [this](const QString& name) {
        if (m_current < 0) return;
        m_images[m_current].title = name;
    });
    connect(m_left, &ControlsPanel::frameSizeChanged, this, [this](int w, int h) {
        if (m_current < 0) return;
        m_images[m_current].state.frameW = w;
        m_images[m_current].state.frameH = h;
        m_playCacheValid = false;
        syncLayersPanel();      // row thumbs follow the frame
        pushPreviewTransform(); // overlay follows the frame
        scheduleRender();
        m_undoTimer.start();
    });
    connect(m_left,    &ControlsPanel::transformChanged, this, &MainWindow::onLayerTransformChanged);
    connect(m_preview, &PreviewWidget::transformChanged, this, [this](const LayerTransform& t) {
        m_transformDragging = true;          // canvas drag: render cheap previews only
        onLayerTransformChanged(t);
    });
    connect(m_preview, &PreviewWidget::groupTransformChanged, this, [this](const QHash<int, LayerTransform>& byId) {
        m_transformDragging = true;
        onGroupTransformChanged(byId);
    });
    connect(m_preview, &PreviewWidget::transformEditFinished, this, [this]() {
        m_transformDragging = false;
        scheduleRender();        // one full-quality pass now that the gesture is done
        m_previewTimer.start();  // refresh layer thumbnails
        syncTimeline();   // dopesheet catches up once the gesture ends
        m_undoTimer.start();
    });
    connect(m_preview, &PreviewWidget::localizationChanged, this, [this](LocParam p, const LocPoint& pt) {
        m_locDragging = true;
        onLocalizationChanged(p, pt);
    });
    connect(m_preview, &PreviewWidget::localizationEditFinished, this, [this]() {
        m_locDragging = false;
        scheduleRender();
        m_previewTimer.start();
        syncTimeline();
        m_undoTimer.start();
    });
    connect(m_preview, &PreviewWidget::localizationDeleteRequested, this, [this](LocParam p) {
        Layer* l = activeLayer();
        if (!l || l->kind != locParamKind(p)) return;
        LocMap& m = (l->kind == LayerKind::DotGrid) ? l->dotGrid.loc
                  : (l->kind == LayerKind::Dither)   ? l->dither.loc
                  : (l->kind == LayerKind::Mosaic)   ? l->mosaic.loc
                                                     : l->ascii.loc;
        m[p].enabled = false;
        m_right->setLocPoint(p, m[p]);
        pushPreviewTransform();
        scheduleRender();
        m_previewTimer.start();
        m_undoTimer.start();
    });
    connect(m_right, &ModePanel::localizationToggleRequested, this, &MainWindow::onLocalizationToggleRequested);
    connect(m_left, &ControlsPanel::localizeToggleRequested, this, &MainWindow::onLocalizeToggleRequested);
    connect(m_preview, &PreviewWidget::selectionChanged, this, &MainWindow::onCanvasSelectionChanged);
    connect(m_right, &ModePanel::paramsChanged,             this, &MainWindow::onParamsChanged);
    connect(m_right, &ModePanel::tonalChanged,              this, &MainWindow::onParamsChanged);
    connect(m_right, &ModePanel::backgroundChanged,         this, &MainWindow::onParamsChanged);
    connect(m_right, &ModePanel::blendChanged, this, [this](BlendMode m) {
        if (const Layer* l = activeLayer()) onLayerBlendChanged(l->id, m);
    });
    connect(m_right, &ModePanel::noModeOpacityChanged, this, &MainWindow::onLayerNoModeOpacityChanged);
    connect(m_right, &ModePanel::modeSelected,              this, &MainWindow::onModeSelected);
    connect(m_right, &ModePanel::clearModeRequested, this, [this]() {
        if (Layer* act = activeLayer()) onLayerRemoveEditsRequested(act->id);
    });
    connect(m_right, &ModePanel::exportRequested,           this, &MainWindow::onExport);

    connect(m_preview,   &PreviewWidget::filesDropped,           this, &MainWindow::onFilesDroppedAsLayer);
    connect(m_preview,   &PreviewWidget::mediaDroppedAsLayer,     this, &MainWindow::onMediaDroppedAsLayer);
    connect(m_filmstrip, &FilmstripWidget::filesDropped,         this, &MainWindow::onFilesDropped);
    connect(m_filmstrip, &FilmstripWidget::addRequested,         this, &MainWindow::onAddRequested);
    connect(m_filmstrip, &FilmstripWidget::thumbSelected,        this, &MainWindow::onThumbSelected);
    connect(m_filmstrip, &FilmstripWidget::thumbActivated,       this, &MainWindow::onThumbActivated);
    connect(m_filmstrip, &FilmstripWidget::thumbCloseRequested,  this, &MainWindow::onThumbCloseRequested);

    // ── Timeline / animation ─────────────────────────────────
    m_timeline->onPlayheadChanged = [this](int f) { setPlayhead(f); };
    m_timeline->onAnimEdited      = [this]() { onTimelineEdited(); };
    m_timeline->onPlayToggled     = [this](bool p) { onPlayToggled(p); };
    m_timeline->onAutoKeyToggled  = [this](bool on) { m_autoKey = on; };
    m_timeline->onImportSequence  = [this]() {
        const QStringList paths = QFileDialog::getOpenFileNames(
            this, "Import sequence", "",
            "Images (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;All Files (*)");
        if (!paths.isEmpty()) importSequence(paths);
    };
    // Playback shows pre-rendered cache frames (built on Play), so it is smooth
    // and never blocks: each tick just swaps the displayed image.
    m_playTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playTimer, &QTimer::timeout, this, [this]() {
        if (!m_playing || m_current < 0) return;

        if (m_playLive) {
            // A mid-play edit can switch a layer onto a CPU-only mode
            // (e.g. Dither → error-diffusion) — drop back to the pre-baked
            // path exactly like the cache-staleness recovery below.
            if (!animCanPlayLive()) {
                m_playTimer.stop();
                m_playLive = false;
                // Long delay: this rebuild happens silently mid-playback (a
                // param edit made the mode CPU-only) — flashing a dialog for
                // every quick rebuild here was the reported "popup on every
                // click" bug. Only a genuinely stalled rebuild should surface it.
                if (!buildPlayCache(2000) || m_playCache.isEmpty()) {
                    m_playing = false;
                    m_timeline->setPlayingSilent(false);
                    scheduleRender();
                    return;
                }
                m_playTimer.start(1000 / qMax(1, m_images[m_current].anim.fps));
                return;
            }
            Animation& a = m_images[m_current].anim;
            int next = a.playhead + 1;
            if (next > a.frameEnd) next = a.frameStart;   // loop
            a.playhead = next;
            m_timeline->setPlayheadSilent(next);
            scheduleRender(/*previewOnly=*/true);
            return;
        }

        // An edit landed mid-playback (layer added/hidden/deleted, param change,
        // …): rebake before the next tick so playback shows the new state. Timer
        // stopped first — the progress dialog processes events and would re-enter.
        SessionImage& img = m_images[m_current];
        if (!m_playCacheValid || m_playCache.isEmpty()
            || img.state != m_playCacheParams || img.anim != m_playCacheAnim) {
            m_playTimer.stop();
            // Long delay — see the m_playLive branch above: this fires on every
            // edit/scrub while a play loop is already running, so a short
            // minimumDuration flashed the dialog constantly.
            if (!buildPlayCache(2000) || m_playCache.isEmpty()) {
                m_playing = false;
                m_timeline->setPlayingSilent(false);
                scheduleRender();
                return;
            }
            m_playTimer.start(1000 / qMax(1, img.anim.fps));
        }
        if (m_playCache.isEmpty()) return;
        Animation& a = m_images[m_current].anim;
        int next = a.playhead + 1;
        if (next > a.frameEnd) next = a.frameStart;   // loop
        a.playhead = next;
        m_timeline->setPlayheadSilent(next);
        const int idx = qBound(0, next - a.frameStart, int(m_playCache.size()) - 1);
        m_preview->setImage(m_playCache[idx]);
    });

    connect(m_layersPanel, &LayersPanel::visibilityToggled, this, &MainWindow::onLayerVisibilityToggled);
    connect(m_layersPanel, &LayersPanel::lockToggled, this, [this](int layerId, bool locked) {
        if (m_current < 0) return;
        auto& layers = m_images[m_current].state.layers;
        const int idx = findLayerById(layers, layerId);
        if (idx < 0) return;
        layers[idx].locked = locked;
        // Drop it from any canvas selection so its gizmo/outline vanishes too.
        if (locked && m_selection.remove(layerId))
            m_preview->setSelection(m_selection, m_images[m_current].state.activeLayerId);
        syncLayersPanel();
        pushPreviewTransform();   // handles/hit-testing pick up the lock
        m_undoTimer.start();      // no render: locking changes no pixels
    });
    connect(m_layersPanel, &LayersPanel::layerSelected,     this, &MainWindow::onLayerSelected);
    connect(m_layersPanel, &LayersPanel::parentSelected,    this, &MainWindow::onParentSelected);
    connect(m_layersPanel, &LayersPanel::layerRangeRequested, this, &MainWindow::onLayerRangeRequested);
    connect(m_layersPanel, &LayersPanel::layerToggleRequested, this, &MainWindow::onLayerToggleRequested);
    connect(m_layersPanel, &LayersPanel::layerRenamed,      this, &MainWindow::onLayerRenamed);
    connect(m_layersPanel, &LayersPanel::deleteRequested,   this, &MainWindow::onLayerDeleteRequested);
    connect(m_layersPanel, &LayersPanel::removeEditsRequested, this, &MainWindow::onLayerRemoveEditsRequested);
    connect(m_layersPanel, &LayersPanel::blendModeChanged,  this, &MainWindow::onLayerBlendChanged);
    connect(m_layersPanel, &LayersPanel::addLayerRequested, this, &MainWindow::onAddLayerRequested);
    connect(m_layersPanel, &LayersPanel::reorderRequested,  this, &MainWindow::onLayerReordered);
    connect(m_layersPanel, &LayersPanel::duplicateChildRequested, this, &MainWindow::onLayerDuplicateRequested);
    connect(m_layersPanel, &LayersPanel::copyLayerRequested,  this, &MainWindow::onCopyLayerRequested);
    connect(m_layersPanel, &LayersPanel::pasteLayerRequested, this, &MainWindow::onPasteLayerRequested);
    connect(m_layersPanel, &LayersPanel::addChildRequested,       this, &MainWindow::onAddChildRequested);
    connect(m_layersPanel, &LayersPanel::mediaDroppedAsLayer,     this, &MainWindow::onMediaDroppedAsLayer);
    connect(m_layersPanel, &LayersPanel::parentReordered,         this, &MainWindow::onParentReordered);
    connect(m_layersPanel, &LayersPanel::groupVisibilityToggled,  this, &MainWindow::onGroupVisibilityToggled);
    connect(m_layersPanel, &LayersPanel::collapseToggled,         this, &MainWindow::onCollapseToggled);
    connect(m_layersPanel, &LayersPanel::duplicateParentRequested,this, &MainWindow::onDuplicateParentRequested);
    connect(m_layersPanel, &LayersPanel::deleteParentRequested,   this, &MainWindow::onDeleteParentRequested);
    connect(m_layersPanel, &LayersPanel::parentRenamed,           this, &MainWindow::onParentRenamed);

    connect(m_worker, &RenderWorker::renderComplete, this, &MainWindow::onRenderComplete);
    connect(m_worker, &RenderWorker::layersComplete, this, &MainWindow::onLayersComplete);

    // ── GPU compositor probe (Phase 1) ───────────────────────
    // Mount the QRhi canvas optimistically (it blits the CPU frames from the
    // start), then after it has had a chance to initialize decide whether the
    // worker should switch to per-layer GPU packages or stay CPU-flattened.
    m_preview->initGpu();
    QTimer::singleShot(700, this, [this]() {
        const bool ok = m_preview->gpuCanvas() && m_preview->gpuCanvas()->isInitialized();
        m_gpuMode = ok;
        if (ok) {
            m_worker->setGpuPackages(true);
            scheduleRender();
        } else {
            m_preview->setGpuActive(false);   // CPU painting, exactly as before
        }
    });

    // ── Undo debounce ────────────────────────────────────────
    m_undoTimer.setSingleShot(true);
    m_undoTimer.setInterval(kUndoDebounceMs);
    connect(&m_undoTimer, &QTimer::timeout, this, &MainWindow::pushUndoSnapshot);

    // ── Layer-thumbnail debounce ─────────────────────────────
    // The big centre preview updates live; only the small layer-panel thumbs
    // wait ~0.5s after the last param edit (rebuilding them live glitched the
    // value boxes).
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(500);
    connect(&m_previewTimer, &QTimer::timeout, this, [this]() {
        if (m_current < 0) return;
        syncLayersPanel();
    });

    // Zoom no longer drives a re-render: the full pass always renders at
    // native frame resolution and the canvas just scales the raster on
    // screen (see RenderWorker::launchFull for why the old zoom-driven
    // supersample was removed).

    // ── Shortcuts ────────────────────────────────────────────
    auto addShortcut = [this](const QKeySequence& seq, auto slot) {
        auto* sc = new QShortcut(seq, this);
        connect(sc, &QShortcut::activated, this, slot);
    };
    addShortcut(QKeySequence("Ctrl+Z"),       &MainWindow::undo);
    addShortcut(QKeySequence("Ctrl+Shift+Z"), &MainWindow::redo);
    addShortcut(QKeySequence("Ctrl+Y"),       &MainWindow::redo);
    addShortcut(QKeySequence("Ctrl+C"),       &MainWindow::copyToClipboard);
    addShortcut(QKeySequence("Ctrl+V"), [this]() {
        QWidget* fw = QApplication::focusWidget();
        if (m_timeline && fw && (fw == m_timeline || m_timeline->isAncestorOf(fw))) {
            m_timeline->pasteKeys();
            return;
        }
        if (m_layersPanel && fw && m_layersPanel->isAncestorOf(fw)) {
            pasteLayerBelowActive();
            return;
        }
        // Image copied from outside the app (browser, file explorer, another
        // editor) → import it into the library and place it as a layer, same
        // as dropping a file onto the canvas.
        const QImage img = QApplication::clipboard()->image();
        if (!img.isNull())
            addLayerFromMedia(addImageToLibrary(img, "Pasted image"));
    });
    addShortcut(QKeySequence("Ctrl+S"), [this]() { saveProject(/*forceDialog=*/false); });
    addShortcut(QKeySequence("Ctrl+Shift+S"), [this]() { saveProject(/*forceDialog=*/true); });
    addShortcut(QKeySequence("Ctrl+O"), &MainWindow::openProject);

    // Empty composition board so the frame is visible on launch, before any
    // image is imported — same board scheduleRender() already draws as a
    // plain background fill when a board has zero layers.
    ensureBoard();
}

MainWindow::~MainWindow() = default;

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    auto* msg = static_cast<MSG*>(message);
    if (!msg) return QMainWindow::nativeEvent(eventType, message, result);

    switch (msg->message) {
    case WM_NCCALCSIZE: {
        if (msg->wParam == FALSE) break;                 // size-only: ignore
        // Maximized borderless windows are positioned by Windows so their edges
        // overhang the monitor by the resize-border thickness; with the frame
        // stripped, that overhang clips our top/left content off-screen. Inset the
        // client by the (per-monitor DPI) frame metrics so content stays on-screen.
        if (::IsZoomed(msg->hwnd)) {
            const UINT dpi = ::GetDpiForWindow(msg->hwnd);
            const int fx = ::GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)
                         + ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            const int fy = ::GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi)
                         + ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
            p->rgrc[0].left   += fx;
            p->rgrc[0].top    += fy;
            p->rgrc[0].right  -= fx;
            p->rgrc[0].bottom -= fy;
        }
        *result = 0;                                     // client area = whole window
        return true;
    }
    case WM_GETMINMAXINFO: {
        // Constrain a maximized window to the work area of the monitor it's on —
        // otherwise a borderless window maximizes to the primary monitor's size
        // (so it can't fill a secondary monitor) and overhangs the taskbar.
        HMONITOR mon = ::MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        if (::GetMonitorInfoW(mon, &mi)) {
            const RECT wk = mi.rcWork, mr = mi.rcMonitor;
            auto* mmi = reinterpret_cast<MINMAXINFO*>(msg->lParam);
            mmi->ptMaxPosition.x  = wk.left - mr.left;    // taskbar offset within monitor
            mmi->ptMaxPosition.y  = wk.top  - mr.top;
            mmi->ptMaxSize.x      = wk.right  - wk.left;
            mmi->ptMaxSize.y      = wk.bottom - wk.top;
            mmi->ptMaxTrackSize.x = wk.right  - wk.left;
            mmi->ptMaxTrackSize.y = wk.bottom - wk.top;
            *result = 0;
            return true;
        }
        break;
    }
    case WM_NCHITTEST: {
        // devicePixelRatioF() can lag behind the monitor the window is
        // currently on (mixed-DPI multi-monitor moves) — read the DPI live
        // off the window itself, same source WM_NCCALCSIZE above uses, so
        // this physical→logical conversion can't drift out of sync with it.
        // A drifted dpr here made canvas clicks occasionally land inside the
        // title bar's mapped rect → HTCAPTION → Windows' native window-drag
        // ghost preview flashing over the canvas.
        const qreal dpr = ::GetDpiForWindow(msg->hwnd) / 96.0;
        const LONG gx = GET_X_LPARAM(msg->lParam);
        const LONG gy = GET_Y_LPARAM(msg->lParam);
        RECT w; ::GetWindowRect(msg->hwnd, &w);
        const int b = qMax(1, qRound(7 * dpr));          // resize-border thickness
        if (!::IsZoomed(msg->hwnd)) {
            const bool L = gx <  w.left  + b, R = gx >= w.right  - b;
            const bool T = gy <  w.top   + b, B = gy >= w.bottom - b;
            if (T && L) { *result = HTTOPLEFT;     return true; }
            if (T && R) { *result = HTTOPRIGHT;    return true; }
            if (B && L) { *result = HTBOTTOMLEFT;  return true; }
            if (B && R) { *result = HTBOTTOMRIGHT; return true; }
            if (L)      { *result = HTLEFT;        return true; }
            if (R)      { *result = HTRIGHT;       return true; }
            if (T)      { *result = HTTOP;         return true; }
            if (B)      { *result = HTBOTTOM;      return true; }
        }
        // Over the custom caption (but not its buttons) → native drag/snap.
        // The vertical/horizontal band is derived from the window's own
        // just-queried rect (w) and the same design-scale formula Ui::px()
        // uses, NOT from m_titleBar->height()/width()/mapFromGlobal(): those
        // route through Qt's cached widget-layout geometry, which can lag a
        // live resize/maximize by a frame (confirmed by logging — a stale
        // rect once reported 960x28 at (50,176) while the real window was
        // still fullscreen). nativeEvent runs synchronously off the raw
        // Win32 message pump, so it can observe that stale geometry mid-
        // transition — a click deep in the canvas would then test against a
        // leftover, wrongly-sized/positioned title-bar rect and be misread
        // as HTCAPTION, flashing Windows' native window-drag ghost preview
        // over the canvas for an ordinary click. Recomputing the band from
        // data fetched in this exact call can't ever be stale.
        if (m_titleBar) {
            const double winWidthLogical = (w.right - w.left) / dpr;
            const int titleH = qRound(44.0 * (winWidthLogical / Ui::kDesignWidth));
            const int localX = qRound((gx - w.left) / dpr);
            const int localY = qRound((gy - w.top)  / dpr);
            if (localY >= 0 && localY < titleH
                && localX >= 0 && localX < qRound(winWidthLogical)
                && !qobject_cast<QPushButton*>(m_titleBar->childAt(localX, localY))) {
                *result = HTCAPTION;
                return true;
            }
        }
        *result = HTCLIENT;
        return true;
    }
    default: break;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif // Q_OS_WIN

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{

    if (obj == this && event->type() == QEvent::WindowStateChange && m_titleBar) {
        m_titleBar->updateMaxIcon();
#ifdef Q_OS_WIN
        // Windows quirk with frameless custom-chrome windows: restoring from
        // the taskbar while maximized can bring the window back at its
        // pre-maximize RECT even though IsZoomed()/isMaximized() still
        // reports true (so the titlebar's restore-icon and showNormal()/
        // showMaximized() toggle both stay "correct" on paper, but the
        // window visibly renders windowed-sized). Nudge Win32 to redo the
        // NCCALCSIZE layout pass (our WM_NCCALCSIZE handler above) so the
        // visible geometry matches the reported state again.
        auto* wsce = static_cast<QWindowStateChangeEvent*>(event);
        const bool wasMinimized = wsce->oldState() & Qt::WindowMinimized;
        const bool nowMinimized = windowState() & Qt::WindowMinimized;
        if (wasMinimized && !nowMinimized && isMaximized()) {
            HWND hwnd = reinterpret_cast<HWND>(winId());
            ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
#endif
    }

    const bool editableFocus = qobject_cast<QLineEdit*>(QApplication::focusWidget()) != nullptr;

    if (!editableFocus) {
        if (event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Space || keyEvent->key() == Qt::Key_CapsLock) {
                event->accept();
                return true;
            }
        }

        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Space) {
                // On the timeline, Space toggles playback instead of panning.
                QWidget* fw = QApplication::focusWidget();
                if (m_timeline && fw && (fw == m_timeline || m_timeline->isAncestorOf(fw))) {
                    if (!keyEvent->isAutoRepeat()) m_timeline->togglePlay();
                    event->accept();
                    return true;
                }
                if (!keyEvent->isAutoRepeat()) {
                    m_spaceDown = true;
                    updatePreviewInteractionState();
                }
                event->accept();
                return true;
            }
            if (keyEvent->key() == Qt::Key_CapsLock) {
                if (!keyEvent->isAutoRepeat()) {
                    m_capsLockActive = !m_capsLockActive;
                    updatePreviewInteractionState();
                }
                event->accept();
                return true;
            }
            // Blender-style "I": hover a parameter control, press I, insert a
            // keyframe there at the current value — no drag/edit needed.
            if (keyEvent->key() == Qt::Key_I && keyEvent->modifiers() == Qt::NoModifier
                && !keyEvent->isAutoRepeat()) {
                if (insertKeyframeUnderCursor()) {
                    event->accept();
                    return true;
                }
            }
            // "H" hides/shows the on-canvas localization dots (points stay
            // enabled; this only declutters the canvas).
            if (keyEvent->key() == Qt::Key_H && keyEvent->modifiers() == Qt::NoModifier
                && !keyEvent->isAutoRepeat()) {
                m_preview->setLocOverlayVisible(!m_preview->locOverlayVisible());
                event->accept();
                return true;
            }
        } else if (event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat()) {
                if (keyEvent->key() == Qt::Key_Space) {
                    m_spaceDown = false;
                    updatePreviewInteractionState();
                    event->accept();
                    return true;
                }
            }
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::updateDisplayedPreview()
{
    if (m_current < 0) {
        m_preview->setImage({});
        return;
    }

    if (m_capsLockActive) {
        const Layer* l = activeLayer();
        const QImage adjustedOnly = ImageAdjuster::apply(
            m_images[m_current].source,
            l ? l->adjustments : Adjustments{});
        const QSize frameSize = (m_images[m_current].state.frameW > 0 && m_images[m_current].state.frameH > 0)
                              ? QSize(m_images[m_current].state.frameW, m_images[m_current].state.frameH)
                              : adjustedOnly.size();
        const QImage originalPlaced = placeOnFramePreview(
            adjustedOnly,
            l ? l->transform : LayerTransform{},
            frameSize);
        m_preview->setShowOriginal(false);
        m_preview->setImage(originalPlaced);
        return;
    }

    m_preview->setShowOriginal(false);

    // GPU compositing: prefer the per-layer packages (full over preview),
    // mirroring the flattened-image priority below.
    if (m_gpuMode) {
        if (m_lastPkgRender.valid)  { m_preview->setGpuPackage(m_lastPkgRender);  return; }
        if (m_lastPkgPreview.valid) { m_preview->setGpuPackage(m_lastPkgPreview); return; }
    }

    if (!m_lastRender.isNull()) {
        m_preview->setImage(m_lastRender);
        return;
    }

    if (!m_lastPreviewFrame.isNull()) {
        m_preview->setImage(m_lastPreviewFrame);
    }
}

void MainWindow::updatePreviewInteractionState()
{
    const bool capsLockActive = m_capsLockActive;

    m_preview->setPanMode(m_spaceDown && !capsLockActive);
    updateDisplayedPreview();

    if (capsLockActive) {
        m_preview->setStatus("Original + adjustments (Caps Lock active)");
    } else {
        m_preview->setStatus(QString());
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    // Backspace / Delete removes the active layer (unless typing in a field).
    if ((event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)
        && !qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        // A selected parent (source image) row deletes the whole group — itself
        // plus every child layer drawing from it — instead of a single layer.
        if (m_selectedParentMediaId >= 0) {
            const int mediaId = m_selectedParentMediaId;
            m_selectedParentMediaId = -1;
            onDeleteParentRequested(mediaId);
            event->accept();
            return;
        }
        // Selected timeline keyframes take precedence over deleting the layer.
        if (m_timeline && m_timeline->deleteSelectedKeys()) {
            event->accept();
            return;
        }
        if (m_current >= 0) {
            const auto& st = m_images[m_current].state;
            // Delete the whole canvas selection, or the active layer if nothing
            // is box-selected. Any kind is deletable (Original images included).
            QList<int> ids = m_selection.values();
            if (ids.isEmpty() && st.activeLayerId >= 0) ids << st.activeLayerId;
            if (!ids.isEmpty()) {
                m_selection.clear();
                for (int id : ids) onLayerDeleteRequested(id);
                event->accept();
                return;
            }
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
    QMainWindow::keyReleaseEvent(event);
}

// ---------------------------------------------------------------------------
// Params <-> panels
// ---------------------------------------------------------------------------

Layer* MainWindow::activeLayer()
{
    if (m_current < 0) return nullptr;
    auto& st = m_images[m_current].state;
    const int idx = findLayerById(st.layers, st.activeLayerId);
    return (idx >= 0) ? &st.layers[idx] : nullptr;
}

const Layer* MainWindow::activeLayer() const
{
    if (m_current < 0) return nullptr;
    const auto& st = m_images[m_current].state;
    const int idx = findLayerById(st.layers, st.activeLayerId);
    return (idx >= 0) ? &st.layers[idx] : nullptr;
}

// ── Cascade helpers ─────────────────────────────────────────────────────────

int MainWindow::addParentMedia(SessionImage& board, const MediaClip& clip)
{
    const int mid = board.nextMediaId++;
    board.media.insert(mid, clip);
    ParentGroup g;
    g.mediaId = mid;
    g.name    = clip.name;
    board.state.parents.push_back(g);
    return mid;
}

Layer MainWindow::makeChildLayer(SessionParams& st, int mediaId, LayerKind kind,
                                 QSize native) const
{
    Layer l;
    l.id      = st.nextLayerId++;
    l.kind    = kind;
    l.mediaId = mediaId;
    l.name    = uniqueLayerName(st, kind, mediaId);
    l.visible = true;
    l.pinned  = true;          // user-created children persist across mode switches
    if (native.isValid() && native.width() > 0 && native.height() > 0)
        l.transform = fitTransform(native.width(), native.height(), st.frameW, st.frameH);
    return l;
}

// Keep children grouped by their parent's order (stable within a group), so the
// composite/tree order stays "all of parents[0]'s children, then parents[1]'s…".
void MainWindow::regroupLayers(SessionParams& st) const
{
    std::stable_sort(st.layers.begin(), st.layers.end(),
        [&st](const Layer& a, const Layer& b) {
            return findParentByMedia(st.parents, a.mediaId)
                 < findParentByMedia(st.parents, b.mediaId);
        });
}

void MainWindow::syncBoardSource(SessionImage& board) const
{
    if (board.state.parents.empty()) { board.source = QImage(); board.frames.clear(); return; }
    const auto it = board.media.find(board.state.parents.front().mediaId);
    if (it != board.media.end()) { board.source = it->image; board.frames = it->frames; }
}

bool MainWindow::groupVisibleFor(const SessionParams& st, int mediaId) const
{
    const int pi = findParentByMedia(st.parents, mediaId);
    return pi < 0 ? true : st.parents[pi].groupVisible;
}

// A child only appears in the frame if it's visible AND its parent group is.
SessionParams MainWindow::bakeGroupVisibility(SessionParams p) const
{
    for (Layer& l : p.layers)
        if (!groupVisibleFor(p, l.mediaId)) l.visible = false;
    return p;
}

void MainWindow::commitStructuralChange()
{
    if (m_current < 0) return;
    m_playCacheValid = false;
    auto& st = m_images[m_current].state;
    regroupLayers(st);
    if (findLayerById(st.layers, st.activeLayerId) < 0)
        st.activeLayerId = st.layers.empty() ? -1 : st.layers.front().id;
    syncBoardSource(m_images[m_current]);
    applyParams(st);
    syncLayersPanel();
    syncTimeline();
    scheduleRender();
    m_undoTimer.start();
}

// Panels → state: writes the panel values into the active layer.
SessionParams MainWindow::collectParams() const
{
    SessionParams p = (m_current >= 0) ? m_images[m_current].state : SessionParams{};

    const int idx = findLayerById(p.layers, p.activeLayerId);
    if (idx >= 0) {
        Layer& l = p.layers[idx];
        l.adjustments = m_left->adjustments();
        // Fill (tonal) lives in the right panel now; inject it into the
        // render struct of the active layer's kind.
        const TonalSettings tonal = m_right->tonalSettings();
        switch (l.kind) {
            case LayerKind::DotGrid: l.dotGrid = m_right->dotGridSettings(); l.dotGrid.tonal = tonal; break;
            case LayerKind::Dither:   l.dither   = m_right->ditherSettings();   l.dither.tonal   = tonal; break;
            case LayerKind::Ascii:    l.ascii    = m_right->asciiSettings();    l.ascii.tonal    = tonal; break;
            case LayerKind::Mosaic:   l.mosaic   = m_right->mosaicSettings();   l.mosaic.tonal   = tonal; break;
            case LayerKind::Halftone: l.halftone = m_right->halftoneSettings(); l.halftone.tonal = tonal; break;
            case LayerKind::Original: break;   // only adjustments apply
        }
    }

    p.background        = m_right->background();
    p.backgroundOpacity = m_right->backgroundOpacity();
    return p;
}

// State → panels: shows the active layer's values.
void MainWindow::applyParams(const SessionParams& p)
{
    // Frame + background are document-level: push them even with zero layers
    // (a freshly loaded or emptied composition), or Frame dimensions/Background
    // silently keep showing whatever the UI happened to have before.
    m_left->setFrameSize(p.frameW > 0 ? p.frameW : 1080,
                         p.frameH > 0 ? p.frameH : 1080);
    m_right->setBackground(p.background, p.backgroundOpacity);

    int idx = findLayerById(p.layers, p.activeLayerId);
    if (idx < 0 && !p.layers.empty()) idx = 0;
    if (idx < 0) { pushPreviewTransform(); refreshAnimationIndicators(); return; }

    m_selectedParentMediaId = -1;   // panels now follow a concrete child again

    const Layer& l = p.layers[idx];
    m_left->setAdjustments(l.adjustments);
    m_left->setTransform(l.transform);   // boxes follow the active layer
    pushPreviewTransform();              // overlay follows the active layer

    // "Localize" button follows the active layer's own mask point.
    const LocMap* maskMap = nullptr;
    switch (l.kind) {
        case LayerKind::Dither: maskMap = &l.dither.loc; break;
        case LayerKind::Ascii:  maskMap = &l.ascii.loc;  break;
        case LayerKind::Mosaic: maskMap = &l.mosaic.loc; break;
        default:                maskMap = &l.dotGrid.loc; break;
    }
    m_left->setLocalizeChecked(locPointOr(*maskMap, maskParamFor(l.kind)).enabled);

    // Fill (tonal), Fusion and enabled-state are handled by setFromLayer.
    m_right->setFromLayer(l);
    refreshAnimationIndicators();
}

void MainWindow::syncLayersPanel()
{
    if (m_current < 0) return;
    SessionImage& board = m_images[m_current];
    auto& st = board.state;
    if (findLayerById(st.layers, st.activeLayerId) < 0 && !st.layers.empty())
        st.activeLayerId = st.layers[0].id;

    // Nothing to add a layer to once the last one's gone (e.g. its source
    // was removed from the library) — hide the "+" until one exists again.
    m_left->setAddLayerVisible(!st.layers.empty());

    // Small source per media for parent + child thumbnails.
    QHash<int, QImage> mediaImages;
    for (const ParentGroup& g : st.parents) {
        const auto it = board.media.find(g.mediaId);
        if (it == board.media.end() || it->image.isNull()) continue;
        mediaImages.insert(g.mediaId,
            it->image.scaled(92, 64, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }

    m_layersPanel->setBackground(st.background, st.backgroundOpacity);
    // Parent/child grouping temporarily hidden — classic flat layer list until
    // it's brought back. The underlying st.parents model is untouched, so
    // reverting to the tree view is just passing st.parents back in here.
    m_layersPanel->setTree({}, st.layers, st.activeLayerId, mediaImages);
    m_layersPanel->setSelection(m_selection);   // reflect multi-select highlight
    m_layersPanel->setSelectedParent(m_selectedParentMediaId);
}

void MainWindow::onParamsChanged()
{
    if (m_current < 0) return;
    m_playCacheValid = false;
    SessionImage& img = m_images[m_current];

    // Baseline currently shown (interpolated frame, or the static state).
    const SessionParams before = img.anim.hasAnimation()
        ? paramsAtFrame(img.state, img.anim, img.anim.playhead)
        : img.state;
    const SessionParams after = collectParams();
    img.state = after;

    // Always run: autoKeyChanged only writes a key when Auto Key is on OR the
    // param already has a track (once animated via "I", it stays animated).
    autoKeyChanged(before, after);
    syncTimeline();   // tracks may have appeared/changed

    scheduleRender();         // centre preview updates live
    m_previewTimer.start();   // layer thumbs catch up once edits settle
    m_undoTimer.start();
}

// Auto-key: write a keyframe at the playhead for every numeric parameter
// whose value changed between the shown baseline and the new panel values.
// Writes when Auto Key is on, OR when the param already has a track — once a
// parameter is animated (e.g. via the "I" key), further edits keep keying it
// even with Auto Key off, matching how "I" is expected to behave afterwards.
void MainWindow::autoKeyChanged(const SessionParams& before, const SessionParams& after)
{
    if (m_current < 0) return;
    Animation& anim = m_images[m_current].anim;
    const int frame = anim.playhead;

    const double bgB = getDocParam(before, ParamId::BackgroundOpacity);
    const double bgA = getDocParam(after,  ParamId::BackgroundOpacity);
    if (bgB != bgA && (m_autoKey || findTrack(anim, -1, ParamId::BackgroundOpacity)))
        upsertKey(anim, -1, ParamId::BackgroundOpacity, frame, bgA);

    // Only the ACTIVE layer can be edited from the panels. collectParams()
    // leaves the other layers at their base values, so comparing them against
    // the interpolated baseline would falsely "detect" changes and write spurious
    // keyframes that flatten the other layers' animation. Key the active layer only.
    const int aid = after.activeLayerId;
    const int ia  = findLayerById(after.layers,  aid);
    const int ib  = findLayerById(before.layers, aid);
    if (ia < 0 || ib < 0) return;
    const Layer& la = after.layers[size_t(ia)];
    const Layer& lb = before.layers[size_t(ib)];
    for (ParamId id : animatableParams(la)) {
        // Transform is keyed separately (autoKeyTransform, from the canvas/box
        // drag handlers): collectParams() never round-trips l.transform, so
        // after.layers[ia].transform is the raw model value, not "as of this
        // frame" — diffing it here against the interpolated baseline would
        // write a spurious keyframe (wrong value) at whatever frame another
        // param happens to be edited on.
        if (id == ParamId::TfX || id == ParamId::TfY
            || id == ParamId::TfScale || id == ParamId::TfRotation)
            continue;
        // Float params are shown through 0..100-step percent sliders (see
        // ModePanel::settings()/setFromLayer()), so an interpolated value gets
        // quantized to ~101 steps on the round-trip through the widget. Compare
        // with half that step as tolerance, or every OTHER param edit at a
        // non-keyframe frame falsely looks "changed" and spams a keyframe here.
        const ParamDesc& d = paramDesc(id);
        const double eps = d.isInt ? 0.0 : (d.hi - d.lo) / 200.0;
        const double a = getParam(la, id), b = getParam(lb, id);
        if (qAbs(a - b) > eps && (m_autoKey || findTrack(anim, aid, id)))
            upsertKey(anim, aid, id, frame, a);
    }
}

// Transform edits (canvas drag) bypass collectParams()/onParamsChanged for
// drag-performance reasons (see onLayerTransformChanged), so they need their
// own auto-key diff instead of going through autoKeyChanged.
void MainWindow::autoKeyTransform(int layerId, const LayerTransform& before, const LayerTransform& after)
{
    if (m_current < 0) return;
    Animation& anim = m_images[m_current].anim;
    const int frame = anim.playhead;

    // Writes when Auto Key is on, OR the field already has a track (same
    // "stays animated once keyed" rule as autoKeyChanged).
    auto key = [&](ParamId id, bool changed, float value) {
        if (changed && (m_autoKey || findTrack(anim, layerId, id)))
            upsertKey(anim, layerId, id, frame, value);
    };
    key(ParamId::TfX,        before.xPct     != after.xPct,     after.xPct);
    key(ParamId::TfY,        before.yPct     != after.yPct,     after.yPct);
    key(ParamId::TfScale,    before.scalePct != after.scalePct, after.scalePct);
    key(ParamId::TfRotation, before.rotation != after.rotation, after.rotation);
}

// Radius/falloff are static (not in ParamId, never keyed) — only the
// Transform-like fields (position/rotation/scale) are animatable, matching
// the on-canvas layer transform.
void MainWindow::autoKeyLocalization(int layerId, LocParam p, const LocPoint& before, const LocPoint& after)
{
    if (m_current < 0) return;
    Animation& anim = m_images[m_current].anim;
    const int frame = anim.playhead;

    auto key = [&](ParamId id, bool changed, float value) {
        if (changed && (m_autoKey || findTrack(anim, layerId, id)))
            upsertKey(anim, layerId, id, frame, value);
    };
    key(locParamId(p, 0), before.posX     != after.posX,     after.posX);
    key(locParamId(p, 1), before.posY     != after.posY,     after.posY);
    key(locParamId(p, 2), before.rotation != after.rotation, after.rotation);
    key(locParamId(p, 3), before.scale    != after.scale,    after.scale);
}

void MainWindow::setPlayhead(int frame)
{
    if (m_current < 0) return;
    SessionImage& img = m_images[m_current];
    frame = qBound(img.anim.frameStart, frame, img.anim.frameEnd);
    img.anim.playhead = frame;

    // Reflect the frame's interpolated parameters in the panels (silent).
    if (img.anim.hasAnimation()) {
        applyParams(paramsAtFrame(img.state, img.anim, frame));
        syncLayersPanel();
    }
    m_timeline->setPlayheadSilent(frame);
    // No keyframes and no video frames: the rendered image is identical at
    // every frame, so scrubbing the playhead has nothing to re-render.
    if (!img.anim.hasAnimation() && img.frames.isEmpty()) return;
    scheduleRender();
}

void MainWindow::syncTimeline()
{
    m_timeline->setAnimation(m_current >= 0 ? m_images[m_current].anim : Animation{});
    refreshAnimationIndicators();
}

// Pushes "this ParamId has a keyframe track on the active layer" to the
// left/right panels, so their labels can tint (see SliderRow::setAnimated).
void MainWindow::refreshAnimationIndicators()
{
    const Layer* l = activeLayer();
    const QSet<ParamId> ids = (m_current >= 0 && l)
        ? animatedParamIds(m_images[m_current].anim, l->id) : QSet<ParamId>{};
    m_left->setAnimatedParams(ids);
    m_right->setAnimatedParams(ids);
}

bool MainWindow::insertKeyframeUnderCursor()
{
    if (m_current < 0) return false;
    Layer* l = activeLayer();
    if (!l) return false;

    QWidget* w = QApplication::widgetAt(QCursor::pos());
    if (!w) return false;

    QHash<QWidget*, ParamId> map = m_left->paramWidgets();
    const QHash<QWidget*, ParamId> rightMap = m_right->paramWidgets();
    for (auto it = rightMap.constBegin(); it != rightMap.constEnd(); ++it)
        map.insert(it.key(), it.value());

    ParamId id = ParamId::AdjBrightness;
    bool found = false;
    for (QWidget* p = w; p; p = p->parentWidget()) {
        auto it = map.constFind(p);
        if (it != map.constEnd()) { id = it.value(); found = true; break; }
    }
    if (!found) return false;

    SessionImage& img = m_images[m_current];
    upsertKey(img.anim, l->id, id, img.anim.playhead, getParam(*l, id));
    syncTimeline();   // also refreshes the animation indicators
    m_undoTimer.start();
    return true;
}

void MainWindow::onTimelineEdited()
{
    if (m_current < 0) return;
    m_playCacheValid = false;
    m_images[m_current].anim = m_timeline->animation();
    refreshAnimationIndicators();
    scheduleRender();
    m_undoTimer.start();
}

// Visible layers whose mode still forces the CPU renderer (error-diffusion
// dither, Braille, Mosaic/Palette text fallbacks, …) block live playback —
// Original layers are always GPU (point-op or the full adjust chain).
static bool layerBlocksLivePlayback(const Layer& l)
{
    if (!l.visible) return false;
    switch (l.kind) {
        case LayerKind::DotGrid:  return !DotGridRenderer::gpuRenderable(l.dotGrid);
        case LayerKind::Halftone: return !HalftoneRenderer::gpuRenderable(l.halftone);
        case LayerKind::Dither:   return !DitherRenderer::gpuRenderable(l.dither);
        case LayerKind::Mosaic:   return !MosaicRenderer::gpuRenderable(l.mosaic);
        case LayerKind::Ascii:    return !AsciiRenderer::gpuRenderable(l.ascii);
        case LayerKind::Original: return false;
    }
    return false;
}

// Checked against the base (un-animated) layer settings: the enums that
// route CPU vs GPU (algorithm, tonal mode, grid shape, per-tone text, …)
// aren't animatable, so they can't drift across the played range.
bool MainWindow::animCanPlayLive() const
{
    if (m_current < 0 || !m_preview->gpuActive()) return false;
    for (const Layer& l : m_images[m_current].state.layers)
        if (layerBlocksLivePlayback(l)) return false;
    return true;
}

void MainWindow::onPlayToggled(bool playing)
{
    if (m_current < 0) { m_playing = false; return; }

    if (playing) {
        m_playLive = animCanPlayLive();
        if (m_playLive) {
            m_playing = true;
            scheduleRender(/*previewOnly=*/true);   // show current frame immediately
            m_playTimer.start(1000 / qMax(1, m_images[m_current].anim.fps));
            return;
        }
        if (!buildPlayCache()) {            // canceled or nothing to play
            m_playing = false;
            m_timeline->setPlayingSilent(false);
            return;
        }
        m_playing = true;
        Animation& a = m_images[m_current].anim;
        const int idx = qBound(0, a.playhead - a.frameStart, int(m_playCache.size()) - 1);
        m_preview->setImage(m_playCache[idx]);   // show current frame immediately
        m_playTimer.start(1000 / qMax(1, a.fps));
    } else {
        m_playing = false;
        m_playLive = false;
        m_playTimer.stop();
        // Hold the frame we're showing as the fallback, or scheduleRender's
        // m_lastRender reset briefly exposes the stale pre-playback preview.
        if (!m_playCache.isEmpty()) {
            const Animation& a = m_images[m_current].anim;
            const int idx = qBound(0, a.playhead - a.frameStart, int(m_playCache.size()) - 1);
            m_lastPreviewFrame = m_playCache[idx];
        }
        scheduleRender();                        // full-resolution render of the current frame
    }
}

// Pre-render every frame at preview resolution so playback is smooth. Returns
// false if there's nothing to play or the user canceled.
bool MainWindow::buildPlayCache(int dialogDelayMs)
{
    if (m_current < 0) return false;
    SessionImage& img = m_images[m_current];
    // Snapshot compare (not just the flag): catches every edit path, including
    // structural ones that never bothered to clear m_playCacheValid.
    if (m_playCacheValid && !m_playCache.isEmpty()
        && img.state == m_playCacheParams && img.anim == m_playCacheAnim)
        return true;

    const int f0 = img.anim.frameStart;
    const int f1 = qMax(f0, img.anim.frameEnd);
    const int count = f1 - f0 + 1;
    if (!img.anim.hasAnimation() && img.frames.isEmpty())
        return false;   // no keyframes, no video frames: every frame is identical

    m_playCache.clear();
    m_playCache.reserve(count);

    AnimProgressDialog progress("Preparing playback…", count, this);
    progress.setMinimumDuration(dialogDelayMs);

    for (int i = 0; i < count; ++i) {
        progress.setValue(i);
        if (progress.wasCanceled()) { m_playCache.clear(); return false; }

        const int frame = steppedFrame(img.anim, f0 + i);
        QImage src = img.source;
        if (!img.frames.isEmpty())
            src = img.frames[qBound(0, frame - f0, img.frames.size() - 1)];
        const SessionParams p = bakeGroupVisibility(img.anim.hasAnimation()
            ? paramsAtFrame(img.state, img.anim, frame)
            : img.state);
        m_playCache.append(m_worker->renderPreviewCached(src, p, RenderWorker::FAST_MAX_PX,
                                                         layerSourcesAt(img, frame)));
    }
    progress.setValue(count);
    m_playCacheValid  = true;
    m_playCacheParams = img.state;
    m_playCacheAnim   = img.anim;
    return true;
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

// Selects a layer; if it is a mode layer, it becomes visible and every
// other layer the user did not pin (turn on by hand) is turned off.
void MainWindow::selectLayerInternal(int layerId, bool makeVisible)
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;
    const int idx = findLayerById(st.layers, layerId);
    if (idx < 0) return;

    st.activeLayerId = layerId;

    if (makeVisible && st.layers[idx].kind != LayerKind::Original) {
        for (Layer& l : st.layers) {
            if (l.id == layerId)  l.visible = true;
            else if (!l.pinned)   l.visible = false;
        }
    }

    applyParams(st);
    m_left->scrollToTop();
    m_right->scrollToTop();
    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
}

// Auto layer names are "<source>.<mode>" (e.g. "example.ascii"): source = the
// parent group's name without extension, mode = the lowercase kind. When the
// name is already taken, 1, 2, 3… is appended.
QString MainWindow::uniqueLayerName(const SessionParams& p, LayerKind kind, int mediaId) const
{
    QString src;
    const int pi = findParentByMedia(p.parents, mediaId);
    if (pi >= 0) src = QFileInfo(p.parents[pi].name).completeBaseName();
    if (src.isEmpty()) src = QStringLiteral("layer");
    const QString base = src + "." + layerKindName(kind).toLower();

    auto exists = [&p](const QString& name) {
        for (const Layer& l : p.layers)
            if (l.name == name) return true;
        return false;
    };
    if (!exists(base)) return base;

    int n = 1;
    QString candidate = base + QString::number(n);
    while (exists(candidate))
        candidate = base + QString::number(++n);
    return candidate;
}

// Duplicating a layer keeps the source's (possibly user-renamed) name instead
// of regenerating "<source>.<mode>" from scratch, so a rename survives its
// copies: "sunset" → "sunset_1", "sunset_2"…
QString MainWindow::uniqueDuplicateName(const SessionParams& p, const QString& baseName) const
{
    auto exists = [&p](const QString& name) {
        for (const Layer& l : p.layers)
            if (l.name == name) return true;
        return false;
    };
    int n = 1;
    QString candidate = baseName + "_" + QString::number(n);
    while (exists(candidate))
        candidate = baseName + "_" + QString::number(++n);
    return candidate;
}

void MainWindow::onModeSelected(RenderMode m)
{
    // No active layer to retarget (no image loaded, or the board has none
    // right now): still remember the pick on the panel itself (new layers
    // always start mode-less regardless — see addLayerFromMedia — but a mode
    // click with nothing active should still show that page, not silently
    // do nothing).
    if (m_current < 0) { m_right->setMode(m); return; }
    auto& st = m_images[m_current].state;
    const LayerKind kind = layerKindForMode(m);

    // The mode tabs change the ACTIVE child's treatment in place, keeping its
    // parent (mediaId), transform and adjustments. The layer carries a settings
    // struct for every kind, so switching just picks which one renders.
    Layer* act = activeLayer();
    if (!act) { m_right->setMode(m); return; }
    if (act->kind == kind) { applyParams(st); return; }

    // Each mode keeps its own `tonal` field, but re-doing the Fill setup after
    // every mode switch is friction — carry whatever is currently shown into
    // the new mode's slot before applyParams() reads it back.
    const TonalSettings tonal = m_right->tonalSettings();
    act->kind = kind;
    act->name = uniqueLayerName(st, kind, act->mediaId);   // name follows the mode
    switch (kind) {
        case LayerKind::DotGrid: act->dotGrid.tonal = tonal; break;
        case LayerKind::Dither:   act->dither.tonal   = tonal; break;
        case LayerKind::Ascii:    act->ascii.tonal    = tonal; break;
        case LayerKind::Mosaic:   act->mosaic.tonal   = tonal; break;
        case LayerKind::Halftone: act->halftone.tonal = tonal; break;
        case LayerKind::Original: break;
    }

    applyParams(st);
    m_right->scrollToTop();   // new mode's section stack starts fresh
    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::onLayerVisibilityToggled(int layerId, bool visible)
{
    if (m_current < 0) return;
    auto& layers = m_images[m_current].state.layers;
    const int idx = findLayerById(layers, layerId);
    if (idx < 0) return;

    layers[idx].visible = visible;
    layers[idx].pinned  = visible;   // hand-toggled layers survive switches

    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::onLayerSelected(int layerId)
{
    if (m_current < 0) return;
    m_selectedParentMediaId = -1;     // picking a child deselects any parent row
    m_selAnchor = layerId;            // anchor for a subsequent shift-range
    m_selection = { layerId };
    if (m_images[m_current].state.activeLayerId == layerId) {
        syncLayersPanel();            // already active → just refresh the highlight
        pushPreviewTransform();
        return;
    }
    selectLayerInternal(layerId, true);
}

// Shift-click a row: select every layer between the anchor and the clicked one
// (inclusive), in stack order. The clicked layer becomes the edit target.
void MainWindow::onLayerRangeRequested(int layerId)
{
    if (m_current < 0) return;
    m_selectedParentMediaId = -1;
    auto& st = m_images[m_current].state;
    const int to = findLayerById(st.layers, layerId);
    if (to < 0) return;
    int from = findLayerById(st.layers, m_selAnchor);
    if (from < 0) { from = to; m_selAnchor = layerId; }

    const int lo = qMin(from, to), hi = qMax(from, to);
    m_selection.clear();
    for (int i = lo; i <= hi; ++i) m_selection.insert(st.layers[i].id);

    st.activeLayerId = layerId;
    applyParams(st);
    m_left->scrollToTop();
    m_right->scrollToTop();
    syncLayersPanel();
    scheduleRender();
}

// Ctrl-click a row: add/remove just that layer from the selection.
void MainWindow::onLayerToggleRequested(int layerId)
{
    if (m_current < 0) return;
    m_selectedParentMediaId = -1;
    auto& st = m_images[m_current].state;
    if (findLayerById(st.layers, layerId) < 0) return;

    if (m_selection.contains(layerId)) {
        m_selection.remove(layerId);
        if (st.activeLayerId == layerId && !m_selection.isEmpty())
            st.activeLayerId = *m_selection.constBegin();
    } else {
        m_selection.insert(layerId);
        st.activeLayerId = layerId;   // newly added becomes the edit target
    }
    m_selAnchor = layerId;
    applyParams(st);
    syncLayersPanel();
    scheduleRender();
}

void MainWindow::onLayerRenamed(int layerId, const QString& name)
{
    if (m_current < 0) return;
    auto& layers = m_images[m_current].state.layers;
    const int idx = findLayerById(layers, layerId);
    if (idx < 0) return;
    layers[idx].name = name;
    syncLayersPanel();
    m_undoTimer.start();
}

void MainWindow::onLayerDeleteRequested(int layerId)
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;

    const int idx = findLayerById(st.layers, layerId);
    if (idx < 0) return;

    // Deleting a layer removes it from the composition; the source stays in the
    // library (filmstrip) so it can be re-added. If this was the last layer of
    // its group, drop the now-empty group too (the source remains in the library).
    const bool wasActive = (st.activeLayerId == layerId);
    const int  mediaId   = st.layers[idx].mediaId;
    st.layers.erase(st.layers.begin() + idx);
    removeLayerTracks(m_images[m_current].anim, layerId);

    const bool stillUsed = std::any_of(st.layers.begin(), st.layers.end(),
        [mediaId](const Layer& l){ return l.mediaId == mediaId; });
    if (!stillUsed) {
        const int pi = findParentByMedia(st.parents, mediaId);
        if (pi >= 0) st.parents.erase(st.parents.begin() + pi);
    }

    if (wasActive && !st.layers.empty())
        selectLayerInternal(st.layers[qMin(idx, int(st.layers.size()) - 1)].id, true);

    syncBoardSource(m_images[m_current]);
    syncLayersPanel();
    syncTimeline();
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::onLayerRemoveEditsRequested(int layerId)
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;
    const int idx = findLayerById(st.layers, layerId);
    if (idx < 0 || st.layers[idx].kind == LayerKind::Original) return;

    Layer& l = st.layers[idx];
    l.kind = LayerKind::Original;     // revert to raw; per-kind settings stay dormant
    l.name = uniqueLayerName(st, LayerKind::Original, l.mediaId);

    if (st.activeLayerId == layerId) applyParams(st);
    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::onLayerBlendChanged(int layerId, BlendMode mode)
{
    if (m_current < 0) return;
    auto& layers = m_images[m_current].state.layers;
    const int idx = findLayerById(layers, layerId);
    if (idx < 0) return;

    layers[idx].blend = mode;
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::onLayerNoModeOpacityChanged(float opacity)
{
    if (m_current < 0) return;
    Layer* l = activeLayer();
    if (!l) return;

    l->opacity = qBound(0.0f, opacity, 1.0f);
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::onLayerTransformChanged(const LayerTransform& t)
{
    if (m_current < 0) return;
    Layer* l = activeLayer();
    if (!l || l->transform == t) return;

    const LayerTransform old = l->transform;
    l->transform = t;
    autoKeyTransform(l->id, old, t);   // cheap: just upserts a float; no-op unless keyed already or Auto Key is on
    m_playCacheValid = false;
    m_left->setTransform(t);   // keep the numeric boxes in sync (silent)
    pushPreviewTransform();
    // During a live canvas drag, only the cheap interactive pass runs (the full
    // pass is deferred to drag end) — re-rendering the whole document, and the
    // layer thumbnails, on every mouse move is what made many-layer drags chaotic.
    // Exception: while magnet-snapped to a frame edge/centre the position is
    // momentarily locked (won't change again until it un-snaps), so a full-
    // quality render is worth it — otherwise the snap settles into a blurry
    // drag preview that visibly sharpens a beat after release.
    const bool preview = m_transformDragging && !m_preview->isSnapped();
    scheduleRender(preview);
    if (!preview) {
        m_previewTimer.start();   // thumbs catch up after edits
        syncTimeline();   // dopesheet catches up too, not every drag frame
    }
    m_undoTimer.start();
}

// locParamKind(p) alone rejects LayerKind::Original, since Original has no
// mode-specific settings struct of its own — but maskParamFor() above still
// routes its whole-layer mask point to DgMask, borrowing dotGrid.loc (see
// layerLocMap below), so Original must be accepted for that one param too —
// otherwise Localize is a dead toggle on a mode-less layer.
static bool locParamMatchesLayer(LayerKind kind, LocParam p)
{
    if (kind == LayerKind::Original) return p == LocParam::DgMask;
    return kind == locParamKind(p);
}

// The LocMap holding LocParam p's point on layer l (settings struct by kind).
static LocMap& layerLocMap(Layer& l, LocParam p)
{
    switch (locParamKind(p)) {
        case LayerKind::Dither: return l.dither.loc;
        case LayerKind::Ascii:  return l.ascii.loc;
        case LayerKind::Mosaic: return l.mosaic.loc;
        default:                return l.dotGrid.loc;
    }
}

// Loc-dot edits bypass collectParams() for the same drag-performance reasons
// as onLayerTransformChanged, but must also keep ModePanel's cached copy in
// sync (the settings() getters round-trip the loc maps, unlike l.transform).
void MainWindow::onLocalizationChanged(LocParam p, const LocPoint& pt)
{
    if (m_current < 0) return;
    Layer* l = activeLayer();
    if (!l || !locParamMatchesLayer(l->kind, p)) return;
    LocMap& m = layerLocMap(*l, p);
    if (locPointOr(m, p) == pt) return;

    const LocPoint old = locPointOr(m, p);
    m[p] = pt;
    autoKeyLocalization(l->id, p, old, pt);
    m_playCacheValid = false;
    m_right->setLocPoint(p, pt);   // keep the panel's round-trip copy current
    pushPreviewTransform();

    const bool preview = m_locDragging;
    scheduleRender(preview);
    if (!preview) {
        m_previewTimer.start();
        syncTimeline();
    }
    m_undoTimer.start();
}

void MainWindow::onLocalizationToggleRequested(LocParam p)
{
    if (m_current < 0) return;
    Layer* l = activeLayer();
    if (!l || !locParamMatchesLayer(l->kind, p)) return;

    LocPoint& pt = layerLocMap(*l, p)[p];
    pt.enabled = !pt.enabled;
    // Re-enabling a hidden overlay would otherwise look like a dead click.
    if (pt.enabled && !m_preview->locOverlayVisible())
        m_preview->setLocOverlayVisible(true);
    m_right->setLocPoint(p, pt);
    pushPreviewTransform();
    scheduleRender();
    m_previewTimer.start();
    m_undoTimer.start();
}

void MainWindow::onLocalizeToggleRequested()
{
    Layer* l = activeLayer();
    if (!l) return;
    onLocalizationToggleRequested(maskParamFor(l->kind));
}

void MainWindow::onGroupTransformChanged(const QHash<int, LayerTransform>& byId)
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;
    bool any = false;
    for (auto it = byId.cbegin(); it != byId.cend(); ++it) {
        const int idx = findLayerById(st.layers, it.key());
        if (idx >= 0 && !(st.layers[idx].transform == it.value())) {
            const LayerTransform old = st.layers[idx].transform;
            st.layers[idx].transform = it.value();
            autoKeyTransform(it.key(), old, it.value());
            any = true;
        }
    }
    if (!any) return;

    m_playCacheValid = false;
    if (const Layer* l = activeLayer()) m_left->setTransform(l->transform);
    pushPreviewTransform();
    const bool preview = m_transformDragging && !m_preview->isSnapped();
    scheduleRender(preview);
    if (!preview) {
        m_previewTimer.start();
        syncTimeline();
    }
    m_undoTimer.start();
}

QSize MainWindow::layerNativeSize(const Layer& l) const
{
    if (m_current < 0) return {};
    const SessionImage& img = m_images[m_current];
    if (l.mediaId >= 0) {
        const auto it = img.media.find(l.mediaId);
        if (it != img.media.end()) {
            const MediaClip& m = it.value();
            if (!m.frames.isEmpty()) return m.frames.front().size();
            if (!m.image.isNull())   return m.image.size();
        }
    }
    return img.source.size();
}

QSize MainWindow::activeLayerNativeSize() const
{
    const Layer* l = activeLayer();
    return l ? layerNativeSize(*l) : QSize();
}

void MainWindow::pushPreviewTransform()
{
    if (m_current < 0) {
        m_preview->setCanvasLayers({}, {});
        m_preview->setSelection({}, -1);
        m_preview->setActiveTransform({}, {}, {}, false);
        m_preview->setLocPoints({}, {});
        return;
    }
    auto& st = m_images[m_current].state;
    const QSize frame(st.frameW, st.frameH);

    // All visible, placeable layers for click/box selection (top-first = UI order).
    QVector<PreviewWidget::CanvasLayer> items;
    for (const Layer& l : st.layers) {
        if (!l.visible) continue;
        const QSize native = layerNativeSize(l);
        if (native.isEmpty()) continue;
        items.push_back({ l.id, l.transform, native, l.locked });
    }
    m_preview->setCanvasLayers(items, frame);

    // Canvas selection is independent of the active layer: nothing is selected
    // by default, only an explicit click selects. Just drop stale ids here.
    const int activeId = st.activeLayerId;
    QSet<int> sel;
    for (int id : m_selection)
        if (findLayerById(st.layers, id) >= 0) sel.insert(id);
    m_selection = sel;
    m_preview->setSelection(sel, activeId);

    const Layer* l = activeLayer();
    const QSize native = activeLayerNativeSize();
    m_preview->setActiveTransform(l ? l->transform : LayerTransform{},
                                  native, frame,
                                  l != nullptr && !native.isEmpty() && !l->locked);

    // Every enabled localization point of the active layer's mode.
    QVector<PreviewWidget::LocEntry> locPts;
    if (l) {
        const LocMap* m = nullptr;
        switch (l->kind) {
            case LayerKind::DotGrid: m = &l->dotGrid.loc; break;
            case LayerKind::Dither:   m = &l->dither.loc;   break;
            case LayerKind::Ascii:    m = &l->ascii.loc;    break;
            case LayerKind::Mosaic:   m = &l->mosaic.loc;   break;
            // Original (mode-less) borrows dotGrid.loc too — see
            // locParamMatchesLayer()/layerLocMap() above.
            default: m = &l->dotGrid.loc; break;
        }
        if (m)
            for (const auto& [p, pt] : *m)
                if (pt.enabled) locPts.push_back({ p, pt });
    }
    m_preview->setLocPoints(locPts, frame);
}

void MainWindow::onCanvasSelectionChanged(const QSet<int>& ids, int activeId)
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;
    m_selection = ids;
    // Selecting an image makes it the param-editing target too; deselecting
    // (empty) leaves the active layer as-is so the panels stay usable.
    if (activeId >= 0 && findLayerById(st.layers, activeId) >= 0)
        st.activeLayerId = activeId;

    applyParams(st);      // refresh panels + pushPreviewTransform (pushes selection)
    // Highlight-only: a canvas click never touches a layer's pixels, so a full
    // syncLayersPanel() (which re-renders every thumbnail, incl. the palette
    // colour-match pass) was doing a wasted synchronous re-render per click —
    // heavy enough with a Fill palette active to briefly freeze/ghost the
    // window. setActiveSelection() just updates which row(s) read selected.
    m_layersPanel->setActiveSelection(st.activeLayerId, m_selection);
    m_layersPanel->setSelectedParent(m_selectedParentMediaId);
    // No scheduleRender(): clicking the canvas only changes which layer is
    // active/selected (a UI concept — activeLayerId isn't read by the render
    // pipeline), never a layer's visibility or content, so the composited
    // image never actually changes here. Re-rendering anyway just flashed the
    // status caption on every click for no visual difference.
}

void MainWindow::onAddLayerRequested()
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;

    const Layer* act = activeLayer();
    if (!act) return;                 // need a parent to attach the new child to

    Layer nl = *act;                  // duplicate the active child (settings + parent)
    nl.id        = st.nextLayerId++;
    nl.kind      = LayerKind::Original; // new children start mode-less; user picks a mode
    nl.name      = uniqueLayerName(st, nl.kind, nl.mediaId);
    nl.visible   = true;
    nl.pinned    = true;
    nl.transform = LayerTransform{};  // fresh layer, not stacked on the source's placement

    const int insertAt = findLayerById(st.layers, act->id);
    st.layers.insert(st.layers.begin() + qBound(0, insertAt + 1, int(st.layers.size())), nl);
    st.activeLayerId = nl.id;
    regroupLayers(st);

    applyParams(st);
    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::onLayerReordered(int layerId, int insertIndex)
{
    if (m_current < 0) return;
    auto& layers = m_images[m_current].state.layers;

    const int from = findLayerById(layers, layerId);
    if (from < 0) return;

    const Layer moved = layers[from];
    layers.erase(layers.begin() + from);
    if (insertIndex > from) --insertIndex;
    insertIndex = qBound(0, insertIndex, int(layers.size()));
    layers.insert(layers.begin() + insertIndex, moved);
    // No regroupLayers() here: that forces layers back into contiguous
    // per-parent blocks, which fights the flat classic list (§ layer panel
    // flattening) where a drag can freely reorder across former groups.

    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
}

// Alt+drag: like onLayerReordered, but the dragged layer is copied in place
// (with a fresh id/name) instead of moved.
void MainWindow::onLayerDuplicateRequested(int layerId, int insertIndex)
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;

    const int from = findLayerById(st.layers, layerId);
    if (from < 0) return;

    Layer nl = st.layers[from];
    nl.id   = st.nextLayerId++;
    nl.name = uniqueDuplicateName(st, nl.name);

    insertIndex = qBound(0, insertIndex, int(st.layers.size()));
    st.layers.insert(st.layers.begin() + insertIndex, nl);
    st.activeLayerId = nl.id;
    // No regroupLayers() — see onLayerReordered.

    applyParams(st);
    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::onCopyLayerRequested(int layerId)
{
    if (m_current < 0) return;
    const auto& layers = m_images[m_current].state.layers;
    const int idx = findLayerById(layers, layerId);
    if (idx < 0) return;

    m_layerClipboard    = layers[idx];
    m_hasLayerClipboard = true;
    m_preview->setStatus("Layer copied");
}

void MainWindow::onPasteLayerRequested(int layerId)
{
    if (m_current < 0 || !m_hasLayerClipboard) return;
    auto& st = m_images[m_current].state;

    const int insertAt = findLayerById(st.layers, layerId);
    if (insertAt < 0) return;   // need a reference row to paste next to

    Layer nl = m_layerClipboard;
    nl.id   = st.nextLayerId++;
    nl.name = uniqueDuplicateName(st, nl.name);

    st.layers.insert(st.layers.begin() + insertAt, nl);
    st.activeLayerId = nl.id;
    regroupLayers(st);

    applyParams(st);
    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
}

// Ctrl+V while a layer row has focus: paste directly under the active layer
// (context-menu "Paste layer" instead pastes above the row it was invoked on).
void MainWindow::pasteLayerBelowActive()
{
    if (m_current < 0 || !m_hasLayerClipboard) return;
    auto& st = m_images[m_current].state;

    const int activeIdx = findLayerById(st.layers, st.activeLayerId);
    if (activeIdx < 0) return;

    Layer nl = m_layerClipboard;
    nl.id   = st.nextLayerId++;
    nl.name = uniqueDuplicateName(st, nl.name);

    st.layers.insert(st.layers.begin() + activeIdx + 1, nl);
    st.activeLayerId = nl.id;
    regroupLayers(st);

    applyParams(st);
    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
    m_preview->setStatus("Layer pasted");
}

// ── Cascade operations (driven by the layer tree) ───────────────────────────

void MainWindow::onAddChildRequested(int mediaId)
{
    if (m_current < 0) return;
    SessionImage& board = m_images[m_current];
    auto& st = board.state;
    if (findParentByMedia(st.parents, mediaId) < 0) return;

    QSize native;
    const auto it = board.media.find(mediaId);
    if (it != board.media.end()) native = it->image.size();

    // New children start as Original (raw); the user picks a mode after.
    Layer child = makeChildLayer(st, mediaId, LayerKind::Original, native);
    st.layers.insert(st.layers.begin(), child);
    st.activeLayerId = child.id;
    commitStructuralChange();
}

void MainWindow::onParentSelected(int mediaId)
{
    if (m_current < 0) return;
    m_selectedParentMediaId = mediaId;
    m_selection.clear();   // clear child highlight so only the parent row reads selected
    m_preview->setSelection(m_selection, m_images[m_current].state.activeLayerId);
    syncLayersPanel();
}

void MainWindow::onParentReordered(int mediaId, int insertIndex)
{
    if (m_current < 0) return;
    auto& parents = m_images[m_current].state.parents;
    const int from = findParentByMedia(parents, mediaId);
    if (from < 0) return;

    const ParentGroup g = parents[from];
    parents.erase(parents.begin() + from);
    if (insertIndex > from) --insertIndex;
    insertIndex = qBound(0, insertIndex, int(parents.size()));
    parents.insert(parents.begin() + insertIndex, g);
    commitStructuralChange();
}

void MainWindow::onGroupVisibilityToggled(int mediaId, bool visible)
{
    if (m_current < 0) return;
    auto& parents = m_images[m_current].state.parents;
    const int pi = findParentByMedia(parents, mediaId);
    if (pi < 0) return;
    parents[pi].groupVisible = visible;
    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::onCollapseToggled(int mediaId, bool collapsed)
{
    if (m_current < 0) return;
    auto& parents = m_images[m_current].state.parents;
    const int pi = findParentByMedia(parents, mediaId);
    if (pi < 0) return;
    parents[pi].collapsed = collapsed;
    syncLayersPanel();   // UI only — no render
}

void MainWindow::onParentRenamed(int mediaId, const QString& name)
{
    if (m_current < 0) return;
    auto& parents = m_images[m_current].state.parents;
    const int pi = findParentByMedia(parents, mediaId);
    if (pi < 0) return;
    parents[pi].name = name;
    syncLayersPanel();
    m_undoTimer.start();
}

void MainWindow::onDuplicateParentRequested(int mediaId)
{
    if (m_current < 0) return;
    SessionImage& board = m_images[m_current];
    auto& st = board.state;
    const int pi = findParentByMedia(st.parents, mediaId);
    const auto mit = board.media.find(mediaId);
    if (pi < 0 || mit == board.media.end()) return;

    const int newMid = board.nextMediaId++;
    board.media.insert(newMid, mit.value());           // implicitly shares pixels
    ParentGroup g = st.parents[pi];
    g.mediaId = newMid;
    g.name    = g.name + " copy";
    st.parents.insert(st.parents.begin() + pi + 1, g);

    // Copy this parent's children onto the new group (new ids, same settings).
    std::vector<Layer> copies;
    for (const Layer& l : st.layers)
        if (l.mediaId == mediaId) {
            Layer c = l;
            c.id      = st.nextLayerId++;
            c.mediaId = newMid;
            copies.push_back(c);
        }
    for (const Layer& c : copies) {
        st.layers.push_back(c);
        // Rebuild the auto name against the new group ("<name> copy"), one at a
        // time so same-mode siblings pick up 1, 2, 3…
        st.layers.back().name = uniqueLayerName(st, c.kind, newMid);
    }
    if (!copies.empty()) st.activeLayerId = copies.front().id;
    commitStructuralChange();
}

void MainWindow::onDeleteParentRequested(int mediaId)
{
    if (m_current < 0) return;
    SessionImage& board = m_images[m_current];
    auto& st = board.state;
    const int pi = findParentByMedia(st.parents, mediaId);
    if (pi < 0) return;

    if (m_selectedParentMediaId == mediaId) m_selectedParentMediaId = -1;

    // Remove the source from the COMPOSITION (its layers + group), but keep it
    // in the library (board.media + filmstrip) so it can be re-added. Use the
    // filmstrip ✕ to remove it from the library entirely.
    for (const Layer& l : st.layers)
        if (l.mediaId == mediaId) removeLayerTracks(board.anim, l.id);
    st.layers.erase(std::remove_if(st.layers.begin(), st.layers.end(),
                    [mediaId](const Layer& l){ return l.mediaId == mediaId; }),
                    st.layers.end());
    st.parents.erase(st.parents.begin() + pi);

    if (findLayerById(st.layers, st.activeLayerId) < 0)
        st.activeLayerId = st.layers.empty() ? -1 : st.layers.front().id;
    syncBoardSource(board);
    commitStructuralChange();
}

void MainWindow::scheduleRender(bool previewOnly, bool qualityOnly)
{
    if (m_current < 0) return;

    // No layers left → nothing to composite, but keep the (empty) frame on
    // screen: show a background-filled canvas at the frame size. The worker bails
    // on a null source, so without this the last frame and its now-empty
    // selection box would linger.
    if (m_images[m_current].state.layers.empty()) {
        const auto& st = m_images[m_current].state;
        const QSize frame(st.frameW > 0 ? st.frameW : 1080,
                          st.frameH > 0 ? st.frameH : 1080);
        QImage bgFrame(frame, QImage::Format_ARGB32_Premultiplied);
        QColor bg = st.background;
        bg.setAlphaF(st.backgroundOpacity);
        bgFrame.fill(bg);
        m_lastRender = bgFrame;
        m_lastPreviewFrame = {};
        m_lastPkgRender = {};
        m_lastPkgPreview = {};
        m_selection.clear();
        m_preview->setImage(bgFrame);
        pushPreviewTransform();
        return;
    }

    // The previous full render is now stale: invalidate it so the fast preview
    // pass (which lands within a few ms) is shown immediately instead of being
    // masked by the old full frame until the 350ms full pass catches up.
    // Exception: a zoom re-render (qualityOnly) keeps the current frame on screen
    // and just upscales it, then swaps in the sharper render when it's ready — so
    // scrolling the zoom doesn't flash back to a low-res preview on every tick.
    if (!qualityOnly) { m_lastRender = {}; m_lastPkgRender = {}; }

    const SessionImage& img = m_images[m_current];
    // The fps dropdown's frame-hold: content (source pixels + baked params)
    // samples this quantized frame, while img.anim.playhead itself always
    // stays the raw native frame (scrubbing/keyframes never see the hold).
    const int frame = steppedFrame(img.anim, img.anim.playhead);

    // Source: a clip uses the playhead's frame; a still uses its image.
    QImage source = img.source;
    if (!img.frames.isEmpty()) {
        const int fi = qBound(0, frame - img.anim.frameStart,
                              img.frames.size() - 1);
        source = img.frames[fi];
    }

    // Parameters: bake the animation at the current playhead, then fold each
    // parent group's master visibility into its children.
    const SessionParams params = bakeGroupVisibility(img.anim.hasAnimation()
        ? paramsAtFrame(img.state, img.anim, frame)
        : img.state);

    // Render the live preview at the size it's actually shown on screen, so the
    // fast pass already matches the (downscaled) final — no jarring quality jump.
    const qreal dpr = m_preview->devicePixelRatioF();
    int previewPx = qMax(qRound(m_preview->width()  * dpr),
                         qRound(m_preview->height() * dpr));
    // While dragging a layer on the canvas, render smaller so each interactive
    // pass is far cheaper (the full-res pass lands once the drag ends).
    if (m_transformDragging) {
        previewPx = qMax(360, previewPx / 2);
    } else if (animCanPlayLive()) {
        // Every layer renders live on GPU, so the interactive pass is cheap even
        // at full frame resolution — run it there, instead of the preview
        // "jumping" when the frame-res full pass lands.
        previewPx = qMax(previewPx, qMax(params.frameW, params.frameH));
    }
    m_worker->setInteractivePreviewPx(previewPx);

    const QHash<int, QImage> ls = layerSourcesAt(img, frame);
    if (qualityOnly)
        // Full pass only — no fast preview pass to flash/jitter during zoom.
        m_worker->requestFullRender(source, params, ls);
    else
        m_worker->requestRender(source, params, /*fullPass=*/!previewOnly, ls);
}

// Resolve, for each media layer, the image it draws at `frame` (a clip indexes
// its frames by the playhead; a still uses its image). Layers without media
// (mediaId < 0) are absent → they fall back to the document base source.
QHash<int, QImage> MainWindow::layerSourcesAt(const SessionImage& img, int frame) const
{
    QHash<int, QImage> out;
    for (const Layer& l : img.state.layers) {
        if (l.mediaId < 0) continue;
        const auto it = img.media.find(l.mediaId);
        if (it == img.media.end()) continue;
        const MediaClip& m = it.value();
        if (!m.frames.isEmpty())
            out.insert(l.id, m.frames[qBound(0, frame - img.anim.frameStart,
                                             int(m.frames.size()) - 1)]);
        else if (!m.image.isNull())
            out.insert(l.id, m.image);
    }
    return out;
}

void MainWindow::onRenderComplete(QImage result, bool isPreview)
{
    // A render that finished after the last layer was deleted is stale: the
    // empty-frame branch in scheduleRender already cleared the canvas — don't
    // let the in-flight result paint the deleted layers back in.
    if (m_current >= 0 && m_images[m_current].state.layers.empty()) return;

    if (isPreview) {
        m_lastPreviewFrame = result;
    } else {
        m_lastRender = result;
    }
    // The "hold to compare original" overlay (source + adjustments at full res)
    // is costly, only used on Caps-Lock, and unaffected by mode params — so
    // refresh it on the full pass only, not on every interactive preview tick.
    if (!isPreview && m_current >= 0 && !m_playing) {
        const Layer* l = activeLayer();
        m_preview->setOriginalImage(ImageAdjuster::apply(
            m_images[m_current].source,
            l ? l->adjustments : Adjustments{}));
    }
    updateDisplayedPreview();
    pushPreviewTransform();
    // A full render landed: clear whatever hint/confirmation text ("Drop
    // images here…", "Layer copied", …) was on screen — there's now an
    // actual result to look at instead.
    if (!isPreview) m_preview->setStatus(QString());
}

// GPU-package twin of onRenderComplete: same bookkeeping, but the frame
// reaches the screen as per-layer textures composited by GpuCanvasWidget.
void MainWindow::onLayersComplete(GpuFramePackage pkg, bool isPreview)
{
    if (m_current >= 0 && m_images[m_current].state.layers.empty()) return;
    if (!pkg.valid) return;

    if (isPreview) m_lastPkgPreview = pkg;
    else           m_lastPkgRender  = pkg;

    if (!isPreview && m_current >= 0 && !m_playing) {
        const Layer* l = activeLayer();
        m_preview->setOriginalImage(ImageAdjuster::apply(
            m_images[m_current].source,
            l ? l->adjustments : Adjustments{}));
    }
    updateDisplayedPreview();
    pushPreviewTransform();
    if (!isPreview) m_preview->setStatus(QString());
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

void MainWindow::pushUndoSnapshot()
{
    if (m_current < 0) return;
    SessionImage& img = m_images[m_current];

    if (img.undoIndex >= 0 && img.undoIndex < img.undoStack.size()
        && img.undoStack[img.undoIndex].params == img.state
        && img.undoStack[img.undoIndex].anim == img.anim)
        return;   // nothing actually changed

    // Drop redo branch
    while (img.undoStack.size() > img.undoIndex + 1)
        img.undoStack.removeLast();

    img.undoStack.append({ img.state, img.anim });
    if (img.undoStack.size() > kMaxUndoSteps)
        img.undoStack.removeFirst();
    img.undoIndex = img.undoStack.size() - 1;
}

void MainWindow::undo()
{
    if (auto* le = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        le->undo();
        return;
    }
    if (m_current < 0) return;
    m_undoTimer.stop();
    pushUndoSnapshot();   // capture pending edits so redo can return here

    SessionImage& img = m_images[m_current];
    if (img.undoIndex <= 0) return;
    --img.undoIndex;
    img.state = img.undoStack[img.undoIndex].params;
    img.anim  = img.undoStack[img.undoIndex].anim;
    syncBoardSource(img);
    m_playCacheValid = false;
    applyParams(img.state);
    syncLayersPanel();
    syncTimeline();
    setPlayhead(img.anim.playhead);
    // applyParams' setters (setTransform, setAdjustments, …) are deliberately
    // silent (m_updating) to avoid feedback loops, so they never schedule a
    // render themselves — undo/redo must trigger one explicitly.
    scheduleRender();
}

void MainWindow::redo()
{
    if (auto* le = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        le->redo();
        return;
    }
    if (m_current < 0) return;
    m_undoTimer.stop();

    SessionImage& img = m_images[m_current];
    if (img.undoIndex >= img.undoStack.size() - 1) return;
    ++img.undoIndex;
    img.state = img.undoStack[img.undoIndex].params;
    img.anim  = img.undoStack[img.undoIndex].anim;
    syncBoardSource(img);
    m_playCacheValid = false;
    applyParams(img.state);
    syncLayersPanel();
    syncTimeline();
    setPlayhead(img.anim.playhead);
    scheduleRender();   // see undo(): applyParams' setters don't self-trigger one
}

void MainWindow::copyToClipboard()
{
    QWidget* fw = QApplication::focusWidget();
    if (m_timeline && fw && (fw == m_timeline || m_timeline->isAncestorOf(fw))) {
        m_timeline->copyKeys();
        m_preview->setStatus("Keyframes copied");
        return;
    }
    if (auto* le = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        le->copy();
        return;
    }
    // A layer row has keyboard focus (clicked in the panel) → copy that layer,
    // not the rendered image.
    if (m_layersPanel && fw && m_layersPanel->isAncestorOf(fw) && m_current >= 0) {
        const int id = m_images[m_current].state.activeLayerId;
        if (id >= 0) onCopyLayerRequested(id);
        return;
    }
    if (!m_lastRender.isNull()) {
        QApplication::clipboard()->setImage(m_lastRender);
        m_preview->setStatus("Copied to clipboard");
    } else if (m_gpuMode && m_lastPkgRender.valid && m_current >= 0) {
        // GPU mode keeps no flattened frame around — compose one on demand
        // through the untouched CPU path (rare operation, exact output).
        const SessionImage& img = m_images[m_current];
        const QImage flat = RenderWorker::renderDocument(
            img.source, img.state, layerSourcesAt(img, steppedFrame(img.anim, img.anim.playhead)));
        if (!flat.isNull()) {
            QApplication::clipboard()->setImage(flat);
            m_preview->setStatus("Copied to clipboard");
        }
    }
}

// ---------------------------------------------------------------------------
// Session images
// ---------------------------------------------------------------------------

QVector<int> MainWindow::addImages(const QStringList& paths)
{
    // A numbered batch (frame_0001.png …) is almost always a video as frames —
    // offer to import it as a single animated clip, like DaVinci's image sequence.
    if (looksLikeSequence(paths)) {
        if (askYesNo(this, QString("These %1 files look like an image sequence.\n\n"
                    "Import them as a single animated clip?").arg(paths.size()))) {
            importSequence(paths); return {};
        }
    }

    // Load a file into a MediaClip (decoding video via ffmpeg).
    auto loadClip = [this](const QString& path, MediaClip& clip) -> bool {
        clip.name = path.startsWith(":/") ? "example" : QFileInfo(path).fileName();
        if (isVideoFile(path)) {
            if (!VideoIO::available()) {
                showMessage(this, "ffmpeg.exe was not found.\n\nPlace ffmpeg.exe next to the application "
                    "to import and export videos.");
                return false;
            }
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QVector<QImage> frames; double fps = 24.0; QString err;
            const bool ok = VideoIO::decode(path, frames, fps, err);
            QApplication::restoreOverrideCursor();
            if (!ok) { showMessage(this, err); return false; }
            clip.frames = frames; clip.image = frames.first(); clip.fps = fps;
            return true;
        }
        QImage im(path);
        if (im.isNull()) {
            showMessage(this, "Could not load image:\n" + path);
            return false;
        }
        clip.image = im;
        return true;
    };

    // Dropped/added files only enter the LIBRARY (board.media + a filmstrip
    // thumbnail). They are not placed in the composition until the user
    // double-clicks or drags a thumbnail onto the Layers panel / canvas.
    const int bi = ensureBoard();
    SessionImage& board = m_images[bi];

    QVector<int> added;
    for (const QString& path : paths) {
        MediaClip clip;
        if (!loadClip(path, clip)) continue;
        const int mid = board.nextMediaId++;
        board.media.insert(mid, clip);
        m_filmstrip->addThumb(mid, clip.image, clip.name);
        added.append(mid);
    }
    if (!added.isEmpty()) m_filmstrip->setActive(added.last());
    return added;
}

// Same library entry as addImages(), for an image that's already in memory
// (clipboard paste) instead of on disk.
int MainWindow::addImageToLibrary(const QImage& img, const QString& name)
{
    const int bi = ensureBoard();
    SessionImage& board = m_images[bi];
    MediaClip clip;
    clip.name  = name;
    clip.image = img;
    const int mid = board.nextMediaId++;
    board.media.insert(mid, clip);
    m_filmstrip->addThumb(mid, clip.image, clip.name);
    m_filmstrip->setActive(mid);
    return mid;
}

// Create the single composition board if it doesn't exist yet, and return its
// index. The board starts with an empty layer stack — sources live in the
// library until placed.
int MainWindow::ensureBoard()
{
    if (m_current >= 0) return m_current;
    SessionImage si;
    si.name  = "composition";
    si.state = SessionParams{};
    si.state.layers.clear();
    si.state.parents.clear();
    si.undoStack.append({ si.state, si.anim });
    si.undoIndex = 0;
    m_images.append(si);
    m_current = m_images.size() - 1;
    m_savedParams = si.state;   // empty board is the "clean" baseline
    m_savedAnim   = si.anim;
    switchToImage(m_current);   // init panels/preview/timeline for the empty board
    return m_current;
}

// Ctrl+S. Video sources aren't supported by the .less file yet: any layer,
// parent group, and animation track that draws from a video media entry is
// silently dropped from what's written (the library still holds the video —
// only the saved file is missing it).
bool MainWindow::saveProject(bool forceDialog)
{
    if (m_current < 0) return true;   // nothing to save
    SessionImage& img = m_images[m_current];

    QString path = m_projectPath;
    if (forceDialog || path.isEmpty()) {
        const QString suggested = path.isEmpty()
            ? (img.title.isEmpty() ? "untitled.less" : img.title + ".less") : path;
        path = QFileDialog::getSaveFileName(this, "Save project", suggested,
                                            "POINTLESS project (*.less)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".less", Qt::CaseInsensitive)) path += ".less";
    }

    QSet<int> stillMediaIds;
    for (auto it = img.media.cbegin(); it != img.media.cend(); ++it)
        if (it.value().frames.isEmpty()) stillMediaIds.insert(it.key());

    ProjectIO::ProjectData data;
    data.title  = img.title;
    data.params = img.state;
    data.anim   = img.anim;

    int skippedVideos = 0;
    for (auto it = img.media.cbegin(); it != img.media.cend(); ++it) {
        if (stillMediaIds.contains(it.key()))
            data.media.insert(it.key(), { it.value().name, it.value().image });
        else
            ++skippedVideos;
    }

    std::vector<Layer> keptLayers;
    QSet<int> keptLayerIds;
    for (const Layer& l : data.params.layers) {
        if (l.mediaId != -1 && !stillMediaIds.contains(l.mediaId)) continue;
        keptLayerIds.insert(l.id);
        keptLayers.push_back(l);
    }
    data.params.layers = keptLayers;

    std::vector<ParentGroup> keptParents;
    for (const ParentGroup& g : data.params.parents)
        if (stillMediaIds.contains(g.mediaId)) keptParents.push_back(g);
    data.params.parents = keptParents;

    std::vector<Track> keptTracks;
    for (const Track& t : data.anim.tracks)
        if (t.layerId == -1 || keptLayerIds.contains(t.layerId)) keptTracks.push_back(t);
    data.anim.tracks = keptTracks;

    QString err;
    if (!ProjectIO::save(path, data, &err)) {
        showMessage(this, "Could not save:\n" + err);
        return false;
    }
    m_projectPath = path;
    m_savedParams = img.state;   // full in-memory state, not the video-stripped `data`:
    m_savedAnim   = img.anim;    // nothing changed session-side just because the file omits video
    m_preview->setStatus(skippedVideos > 0
        ? QString("Saved (skipped %1 video layer%2 — not supported yet)")
              .arg(skippedVideos).arg(skippedVideos == 1 ? "" : "s")
        : "Saved");
    return true;
}

// Ctrl+O. Replaces the current composition — this app has one board at a
// time, no multi-project tabs.
void MainWindow::openProject()
{
    const QString path = QFileDialog::getOpenFileName(this, "Open project", "",
                                                       "POINTLESS project (*.less)");
    if (path.isEmpty()) return;
    openProjectFromPath(path);
}

// Shared by the Ctrl+O dialog and a path handed in on the command line
// (double-clicking a .less file once file association is registered).
void MainWindow::openProjectFromPath(const QString& path)
{
    ProjectIO::ProjectData data;
    QString err;
    if (!ProjectIO::load(path, &data, &err)) {
        showMessage(this, "Could not open:\n" + err);
        return;
    }

    m_filmstrip->clear();
    m_images.clear();
    m_current = -1;

    SessionImage si;
    si.title = data.title;
    si.name  = data.title;
    si.state = data.params;
    si.anim  = data.anim;

    int maxMediaId = 0;
    for (auto it = data.media.cbegin(); it != data.media.cend(); ++it) {
        MediaClip clip;
        clip.name  = it.value().name;
        clip.image = it.value().image;
        si.media.insert(it.key(), clip);
        maxMediaId = qMax(maxMediaId, it.key());
        m_filmstrip->addThumb(it.key(), clip.image, clip.name);
    }
    si.nextMediaId = maxMediaId + 1;
    syncBoardSource(si);
    si.undoStack.append({ si.state, si.anim });
    si.undoIndex = 0;

    m_images.append(si);
    m_current = 0;
    m_projectPath = path;
    m_savedParams = si.state;
    m_savedAnim   = si.anim;
    switchToImage(m_current);
}

bool MainWindow::isDirty() const
{
    if (m_current < 0) return false;
    const SessionImage& img = m_images[m_current];
    return img.state != m_savedParams || img.anim != m_savedAnim;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!isDirty()) { event->accept(); return; }

    const QString name = (m_current >= 0 && !m_images[m_current].title.isEmpty())
        ? m_images[m_current].title : "Untitled";
    UnsavedChangesDialog dlg(name, this);
    dlg.exec();

    if (dlg.choice() == UnsavedChangesDialog::Cancel) { event->ignore(); return; }
    if (dlg.choice() == UnsavedChangesDialog::Save && !saveProject(/*forceDialog=*/false)) {
        event->ignore();   // Save As dialog was cancelled, or the write failed
        return;
    }
    event->accept();
}

// Place a library source into the composition as a new layer (creating its
// parent group the first time it's used).
void MainWindow::addLayerFromMedia(int mediaId)
{
    const int bi = ensureBoard();
    SessionImage& board = m_images[bi];
    const auto it = board.media.find(mediaId);
    if (it == board.media.end()) return;
    const MediaClip clip = it.value();
    auto& st = board.state;

    if (findParentByMedia(st.parents, mediaId) < 0) {
        ParentGroup g;
        g.mediaId = mediaId;
        g.name    = clip.name;
        st.parents.push_back(g);
    }

    // Mode-less by default (like onAddLayerRequested) — dropping a new source
    // shouldn't inherit whatever mode happens to be showing in the panel.
    Layer child = makeChildLayer(st, mediaId, LayerKind::Original,
                                 clip.image.size());
    st.layers.insert(st.layers.begin(), child);
    st.activeLayerId = child.id;

    // A video source defines the timeline range when first placed.
    if (!clip.frames.isEmpty() && board.anim.frameEnd <= 1) {
        board.anim.frameStart = 0;
        board.anim.frameEnd   = clip.frames.size() - 1;
        board.anim.fps        = qBound(1, qRound(clip.fps), 240);
        syncTimeline();
    }

    syncBoardSource(board);
    m_left->setSourceImage(board.source);
    m_right->setSourceImage(board.source);
    m_filmstrip->setActive(mediaId);
    commitStructuralChange();
}

void MainWindow::importSequence(const QStringList& paths)
{
    QStringList sorted = paths;
    sorted.sort();   // order frames by filename

    QVector<QImage> frames;
    for (const QString& p : sorted) {
        QImage im(p);
        if (!im.isNull()) frames.append(im);
    }
    if (frames.isEmpty()) {
        showMessage(this, "No valid images in the selection.");
        return;
    }

    // A sequence is one video-like clip in the library; place it as a layer so
    // it shows and defines the timeline range.
    const int bi = ensureBoard();
    SessionImage& board = m_images[bi];

    MediaClip clip;
    clip.name   = QString("sequence (%1)").arg(frames.size());
    clip.image  = frames.first();
    clip.frames = frames;
    clip.fps    = 24.0;

    const int mid = board.nextMediaId++;
    board.media.insert(mid, clip);
    m_filmstrip->addThumb(mid, clip.image, clip.name);
    addLayerFromMedia(mid);
}

void MainWindow::switchToImage(int index)
{
    m_playCacheValid = false;
    m_playCache.clear();
    if (index < 0 || index >= m_images.size()) {
        m_current = -1;
        m_lastRender = {};
        m_lastPreviewFrame = {};
        m_lastPkgRender = {};
        m_lastPkgPreview = {};
        m_capsLockActive = false;
        m_spaceDown = false;
        m_preview->setPanMode(false);
        m_preview->setShowOriginal(false);
        m_preview->resetZoom();
        m_preview->setImage({});
        m_filmstrip->setActive(-1);
        m_left->setSourceImage({});
        m_left->setAdjustments(Adjustments{});   // else Levels keep showing the last image's points
        m_right->setSourceImage({});
        m_left->setFileName(QString());
        m_playTimer.stop();
        m_playing = false;
        m_timeline->setAnimation(Animation{});
        // Keep the (embedded) list widget in the layout — hiding it would drop
        // its stretch slot and let the "Layers" header balloon. Just clear it.
        m_layersPanel->setTree({}, {}, -1, {});
        m_left->setAddLayerVisible(false);   // nothing to add a layer to
        return;
    }
    m_current = index;
    m_playTimer.stop();
    m_playing = false;
    m_left->setSourceImage(m_images[index].source);
    m_right->setSourceImage(m_images[index].source);
    m_left->setFileName(m_images[index].title);
    applyParams(m_images[index].state);
    m_left->scrollToTop();
    m_right->scrollToTop();
    m_filmstrip->setActive(index);
    m_left->setAddLayerVisible(true);
    m_layersPanel->setVisible(true);
    m_layersPanel->setSourceImage(m_images[index].source);
    syncLayersPanel();
    m_lastRender = {};
    m_lastPreviewFrame = {};
    m_lastPkgRender = {};
    m_lastPkgPreview = {};
    m_spaceDown = false;
    m_capsLockActive = false;
    m_preview->setPanMode(false);
    m_preview->setShowOriginal(false);
    m_preview->resetZoom();
    syncTimeline();
    setPlayhead(m_images[index].anim.playhead);   // applies interpolated params + renders
}

void MainWindow::onAddRequested()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, "Add images or video", "",
        "Images & video (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.tif *.tiff "
        "*.mp4 *.mov *.avi *.mkv *.webm *.m4v);;"
        "Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.tif *.tiff);;"
        "Video (*.mp4 *.mov *.avi *.mkv *.webm *.m4v);;All Files (*)");
    if (!paths.isEmpty()) addImages(paths);
}

void MainWindow::onFilesDropped(const QStringList& paths)
{
    addImages(paths);
}

void MainWindow::onFilesDroppedAsLayer(const QStringList& paths)
{
    for (int mid : addImages(paths))
        addLayerFromMedia(mid);
}

void MainWindow::onThumbSelected(int mediaId)
{
    // Library: single click just highlights the source. Use double-click or drag
    // to place it as a layer.
    m_filmstrip->setActive(mediaId);
}

void MainWindow::onThumbActivated(int mediaId)
{
    addLayerFromMedia(mediaId);
}

void MainWindow::onMediaDroppedAsLayer(int mediaId)
{
    addLayerFromMedia(mediaId);
}

void MainWindow::onThumbCloseRequested(int mediaId)
{
    if (m_current < 0) return;
    SessionImage& board = m_images[m_current];
    auto& st = board.state;

    const bool inUse = std::any_of(st.layers.begin(), st.layers.end(),
        [mediaId](const Layer& l){ return l.mediaId == mediaId; });
    if (inUse) {
        if (!askYesNo(this, "This source is used by one or more layers.\n\nRemove it and its layers?",
                      /*defaultYes=*/false))
            return;
    }

    for (const Layer& l : st.layers)
        if (l.mediaId == mediaId) removeLayerTracks(board.anim, l.id);
    st.layers.erase(std::remove_if(st.layers.begin(), st.layers.end(),
                    [mediaId](const Layer& l){ return l.mediaId == mediaId; }),
                    st.layers.end());
    const int pi = findParentByMedia(st.parents, mediaId);
    if (pi >= 0) st.parents.erase(st.parents.begin() + pi);
    board.media.remove(mediaId);
    m_filmstrip->removeThumb(mediaId);

    if (findLayerById(st.layers, st.activeLayerId) < 0)
        st.activeLayerId = st.layers.empty() ? -1 : st.layers.front().id;
    syncBoardSource(board);
    commitStructuralChange();
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

void MainWindow::onExport()
{
    if (m_current < 0) {
        showMessage(this, "No image loaded.");
        return;
    }

    const QImage&       source = m_images[m_current].source;
    const SessionParams params = m_images[m_current].state;

    QString format = m_right->outputFormat().toLower();
    QString name   = m_images[m_current].title.trimmed();   // from the title top-left
    if (name.isEmpty()) name = "output";

    if (format == "png sequence") {
        exportSequence(name);
        return;
    }
    if (format == "mp4") {
        exportVideoMp4(name);
        return;
    }
    if (format == "svg") {
        exportSvg(name);
        return;
    }

    QString filter;
    if      (format == "png") filter = "PNG Image (*.png)";
    else if (format == "jpg") filter = "JPEG Image (*.jpg)";

    QString savePath = QFileDialog::getSaveFileName(
        this, "Export", name + "." + format, filter);
    if (savePath.isEmpty()) return;

    QImage canvas = RenderWorker::renderDocument(source, params);

    if (format == "jpg") {
        // JPEG has no alpha — flatten on white
        QImage flat(canvas.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter fp(&flat);
        fp.drawImage(0, 0, canvas);
        fp.end();
        canvas = flat;
    }

    int quality = (format == "jpg") ? 95 : -1;
    if (!canvas.save(savePath, format.toUpper().toUtf8().constData(), quality)) {
        showMessage(this, "Could not save file:\n" + savePath);
        return;
    }

    m_preview->setStatus("Exported: " + savePath);
}

// Export the current frame as a vector SVG (halftone/ascii/dither → shapes).
void MainWindow::exportSvg(const QString& baseName)
{
    if (m_current < 0) return;
    const SessionImage& img = m_images[m_current];

    const int frame = steppedFrame(img.anim, img.anim.playhead);
    QImage source = img.source;
    if (!img.frames.isEmpty()) {
        const int fi = qBound(0, frame - img.anim.frameStart, img.frames.size() - 1);
        source = img.frames[fi];
    }
    const SessionParams params = bakeGroupVisibility(img.anim.hasAnimation()
        ? paramsAtFrame(img.state, img.anim, frame)
        : img.state);
    const QHash<int, QImage> ls = layerSourcesAt(img, frame);

    // Heavy-render guard: a fine grid / small dither cell can produce hundreds of
    // thousands of shapes — a huge file that may lag or crash while writing.
    const int elements = RenderWorker::estimateSvgElements(source, params, ls);
    if (elements > 150000) {
        if (!askYesNo(this, QString("This export contains roughly %1 vector shapes.\n\n"
                    "The SVG may be very large and slow to open, and the app "
                    "could lag or run out of memory while writing it.\n\nContinue?")
                .arg(elements), /*defaultYes=*/false))
            return;
    }

    const QString savePath = QFileDialog::getSaveFileName(
        this, "Export SVG", baseName + ".svg", "SVG Image (*.svg)");
    if (savePath.isEmpty()) return;

    if (!RenderWorker::renderDocumentToSvg(savePath, source, params, ls)) {
        showMessage(this, "Could not write SVG:\n" + savePath);
        return;
    }
    m_preview->setStatus("Exported: " + savePath);
}

// Render every frame [frameStart, frameEnd] (clip frame + animated params)
// to numbered PNG files in a chosen folder.
void MainWindow::exportSequence(const QString& baseName)
{
    if (m_current < 0) return;
    SessionImage& img = m_images[m_current];
    const int f0 = img.anim.frameStart;
    const int f1 = qMax(f0, img.anim.frameEnd);
    const int count = f1 - f0 + 1;

    const QString dir = QFileDialog::getExistingDirectory(this, "Export PNG sequence");
    if (dir.isEmpty()) return;

    const int digits = qMax(4, QString::number(f1).size());
    AnimProgressDialog progress("Rendering frames…", count, this);

    int written = 0;
    for (int i = 0; i < count; ++i) {
        progress.setValue(i);
        if (progress.wasCanceled()) break;

        const int rawFrame = f0 + i;
        const int frame    = steppedFrame(img.anim, rawFrame);
        QImage src = img.source;
        if (!img.frames.isEmpty()) {
            const int fi = qBound(0, frame - f0, img.frames.size() - 1);
            src = img.frames[fi];
        }
        const SessionParams p = bakeGroupVisibility(img.anim.hasAnimation()
            ? paramsAtFrame(img.state, img.anim, frame)
            : img.state);

        const QImage canvas = m_worker->renderDocumentInteractive(src, p, layerSourcesAt(img, frame));
        const QString fn = QString("%1/%2_%3.png")
            .arg(dir, baseName, QString::number(rawFrame).rightJustified(digits, '0'));
        if (canvas.save(fn, "PNG")) ++written;
    }
    progress.setValue(count);

    m_preview->setStatus(QString("Exported %1 frames to %2").arg(written).arg(dir));
}

// Render every frame to a temp PNG sequence, then encode it to an H.264 mp4
// (video only) with the bundled ffmpeg.
void MainWindow::exportVideoMp4(const QString& baseName)
{
    if (m_current < 0) return;
    if (!VideoIO::available()) {
        showMessage(this, "ffmpeg.exe was not found.\n\nPlace ffmpeg.exe next to the application to export videos.");
        return;
    }

    SessionImage& img = m_images[m_current];
    const int f0 = img.anim.frameStart;
    const int f1 = qMax(f0, img.anim.frameEnd);
    const int count = f1 - f0 + 1;

    const QString savePath = QFileDialog::getSaveFileName(
        this, "Export MP4", baseName + ".mp4", "MP4 Video (*.mp4)");
    if (savePath.isEmpty()) return;

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        showMessage(this, "Could not create a temporary folder.");
        return;
    }

    AnimProgressDialog progress("Rendering frames…", count, this);
    for (int i = 0; i < count; ++i) {
        progress.setValue(i);
        if (progress.wasCanceled()) return;

        const int frame = steppedFrame(img.anim, f0 + i);
        QImage src = img.source;
        if (!img.frames.isEmpty())
            src = img.frames[qBound(0, frame - f0, img.frames.size() - 1)];
        const SessionParams p = bakeGroupVisibility(img.anim.hasAnimation()
            ? paramsAtFrame(img.state, img.anim, frame)
            : img.state);

        QImage canvas = m_worker->renderDocumentInteractive(src, p, layerSourcesAt(img, frame));
        // mp4 (yuv420p) has no alpha — flatten on an opaque background.
        QImage flat(canvas.size(), QImage::Format_RGB32);
        QColor bg = p.background; bg.setAlpha(255);
        flat.fill(bg);
        QPainter fp(&flat);
        fp.drawImage(0, 0, canvas);
        fp.end();
        flat.save(QString("%1/f_%2.png").arg(tmp.path(),
                  QString::number(i).rightJustified(6, '0')), "PNG");
    }
    progress.setValue(count);

    progress.setLabelText("Encoding video…");
    progress.setRange(0, 0);   // busy indicator
    QApplication::processEvents();

    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = VideoIO::encodePngDir(tmp.path(), "f_%06d.png", img.anim.fps, savePath, err);
    QApplication::restoreOverrideCursor();
    progress.close();

    if (!ok) { showMessage(this, err); return; }
    m_preview->setStatus("Exported video: " + savePath);
}
