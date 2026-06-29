#include "MainWindow.h"
#include "PreviewWidget.h"
#include "ControlsPanel.h"
#include "UiScale.h"
#include "ModePanel.h"
#include "FilmstripWidget.h"
#include "TimelineWidget.h"
#include "LayersPanel.h"
#include "Widgets.h"
#include "../workers/RenderWorker.h"
#include "../core/ImageAdjuster.h"
#include "../core/VideoIO.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProgressDialog>
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
        setFixedHeight(Ui::px(44));

        auto* hl = new QHBoxLayout(this);
        // Left gutter 40 = left column gutter; right gutter 14 = right column +/-.
        hl->setContentsMargins(Ui::px(40), 0, Ui::px(14), 0);
        hl->setSpacing(0);

        const int lh = Ui::px(34);
        auto* logo = new QLabel;
        logo->setObjectName("titleLogo");
        logo->setPixmap(QIcon(":/logo.png").pixmap(QSize(lh, lh)));
        logo->setAttribute(Qt::WA_TransparentForMouseEvents);  // drag through it
        hl->addWidget(logo);
        hl->addStretch(1);

        auto mkBtn = [&](const QString& icon, const char* obj) {
            auto* b = new QPushButton;
            b->setObjectName(obj);
            b->setCursor(Qt::PointingHandCursor);
            b->setFixedSize(Ui::px(48), Ui::px(32));
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
    setWindowTitle("ULTRA TOOL — Ditherer");
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

    // Bottom panel: "Timeline | Library" tabs over a stacked area.
    auto* bottomPanel = new QWidget;
    bottomPanel->setObjectName("bottomBar");
    bottomPanel->setMinimumHeight(130);   // never collapses away
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
    trl->setContentsMargins(Ui::px(20), Ui::px(10), Ui::px(20), Ui::px(6));
    trl->setSpacing(Ui::px(6));
    auto* tabTimeline = new QPushButton("Timeline");
    auto* tabLibrary  = new QPushButton("Library");
    for (QPushButton* b : { tabTimeline, tabLibrary }) {
        b->setObjectName("rectTab");
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setCursor(Qt::PointingHandCursor);
    }
    tabTimeline->setChecked(true);
    trl->addWidget(tabTimeline);
    trl->addWidget(tabLibrary);
    trl->addStretch(1);

    auto* bottomStack = new QStackedWidget;
    bottomStack->addWidget(m_timeline);    // page 0 (default)
    bottomStack->addWidget(m_filmstrip);   // page 1 (Library)
    connect(tabTimeline, &QPushButton::clicked, this, [bottomStack] { bottomStack->setCurrentIndex(0); });
    connect(tabLibrary,  &QPushButton::clicked, this, [bottomStack] { bottomStack->setCurrentIndex(1); });
    bottomStack->setCurrentIndex(0);   // default to Timeline

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
    centerSplit->setSizes({ 600, 220 });

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
    mainSplit->setSizes({ Ui::px(420), Ui::px(1718), Ui::px(420) });

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
        m_undoTimer.start();
    });
    connect(m_preview, &PreviewWidget::selectionChanged, this, &MainWindow::onCanvasSelectionChanged);
    connect(m_right, &ModePanel::paramsChanged,             this, &MainWindow::onParamsChanged);
    connect(m_right, &ModePanel::tonalChanged,              this, &MainWindow::onParamsChanged);
    connect(m_right, &ModePanel::backgroundChanged,         this, &MainWindow::onParamsChanged);
    connect(m_right, &ModePanel::blendChanged, this, [this](BlendMode m) {
        if (const Layer* l = activeLayer()) onLayerBlendChanged(l->id, m);
    });
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
        if (!m_playing || m_current < 0 || m_playCache.isEmpty()) return;
        Animation& a = m_images[m_current].anim;
        int next = a.playhead + 1;
        if (next > a.frameEnd) next = a.frameStart;   // loop
        a.playhead = next;
        m_timeline->setPlayheadSilent(next);
        const int idx = qBound(0, next - a.frameStart, int(m_playCache.size()) - 1);
        m_preview->setImage(m_playCache[idx]);
    });

    connect(m_layersPanel, &LayersPanel::visibilityToggled, this, &MainWindow::onLayerVisibilityToggled);
    connect(m_layersPanel, &LayersPanel::layerSelected,     this, &MainWindow::onLayerSelected);
    connect(m_layersPanel, &LayersPanel::layerRangeRequested, this, &MainWindow::onLayerRangeRequested);
    connect(m_layersPanel, &LayersPanel::layerToggleRequested, this, &MainWindow::onLayerToggleRequested);
    connect(m_layersPanel, &LayersPanel::layerRenamed,      this, &MainWindow::onLayerRenamed);
    connect(m_layersPanel, &LayersPanel::deleteRequested,   this, &MainWindow::onLayerDeleteRequested);
    connect(m_layersPanel, &LayersPanel::removeEditsRequested, this, &MainWindow::onLayerRemoveEditsRequested);
    connect(m_layersPanel, &LayersPanel::blendModeChanged,  this, &MainWindow::onLayerBlendChanged);
    connect(m_layersPanel, &LayersPanel::addLayerRequested, this, &MainWindow::onAddLayerRequested);
    connect(m_layersPanel, &LayersPanel::reorderRequested,  this, &MainWindow::onLayerReordered);
    connect(m_layersPanel, &LayersPanel::addChildRequested,       this, &MainWindow::onAddChildRequested);
    connect(m_layersPanel, &LayersPanel::mediaDroppedAsLayer,     this, &MainWindow::onMediaDroppedAsLayer);
    connect(m_layersPanel, &LayersPanel::parentReordered,         this, &MainWindow::onParentReordered);
    connect(m_layersPanel, &LayersPanel::groupVisibilityToggled,  this, &MainWindow::onGroupVisibilityToggled);
    connect(m_layersPanel, &LayersPanel::collapseToggled,         this, &MainWindow::onCollapseToggled);
    connect(m_layersPanel, &LayersPanel::duplicateParentRequested,this, &MainWindow::onDuplicateParentRequested);
    connect(m_layersPanel, &LayersPanel::deleteParentRequested,   this, &MainWindow::onDeleteParentRequested);
    connect(m_layersPanel, &LayersPanel::parentRenamed,           this, &MainWindow::onParentRenamed);

    connect(m_worker, &RenderWorker::renderComplete, this, &MainWindow::onRenderComplete);
    connect(m_worker, &RenderWorker::renderStarted, this, [this](bool isPreview) {
        m_preview->setStatus(isPreview ? "Preview…" : "Rendering full resolution…");
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

    // Re-render at the zoomed resolution once the user stops scrolling, so the
    // vector symbols sharpen up (the existing raster is shown upscaled meanwhile).
    m_zoomRenderTimer.setSingleShot(true);
    m_zoomRenderTimer.setInterval(150);
    connect(&m_zoomRenderTimer, &QTimer::timeout, this, [this]() {
        // Nothing to gain (zoomed out, or a dither layer that mustn't re-process)
        // and a full frame is already shown → keep it, don't re-render.
        if (zoomQualityScale() <= 1.01f && !m_lastRender.isNull()) return;
        scheduleRender(/*previewOnly=*/false, /*qualityOnly=*/true);
    });
    connect(m_preview, &PreviewWidget::zoomChanged, this,
            [this]() { m_zoomRenderTimer.start(); });

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
        if (m_timeline && fw && (fw == m_timeline || m_timeline->isAncestorOf(fw)))
            m_timeline->pasteKeys();
    });

    // ── Demo image ───────────────────────────────────────────
    // Keep one image loaded for convenience: add it to the library and place it
    // as a layer so there's something on screen without importing.
    addImages({ ":/example.jpg" });
    if (m_current >= 0) {
        const auto ids = m_images[m_current].media.keys();
        if (!ids.isEmpty()) addLayerFromMedia(ids.first());
    }
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
        auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
        if (::IsZoomed(msg->hwnd)) {
            // A maximized borderless window overhangs the monitor by the frame
            // thickness; inset so its content and the taskbar stay visible.
            const int fx = ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
            const int fy = ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
            RECT& rc = p->rgrc[0];
            rc.left += fx; rc.right -= fx; rc.top += fy; rc.bottom -= fy;
        }
        *result = 0;                                     // client area = whole window
        return true;
    }
    case WM_NCHITTEST: {
        const qreal dpr = devicePixelRatioF();
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
        if (m_titleBar) {
            const QPoint local = m_titleBar->mapFromGlobal(
                QPoint(qRound(gx / dpr), qRound(gy / dpr)));
            if (m_titleBar->rect().contains(local)
                && !qobject_cast<QPushButton*>(m_titleBar->childAt(local))) {
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
    if (obj == this && event->type() == QEvent::WindowStateChange && m_titleBar)
        m_titleBar->updateMaxIcon();

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
        m_preview->setOriginalImage(adjustedOnly);
        m_preview->setShowOriginal(true);
        m_preview->setImage(adjustedOnly);
        return;
    }

    m_preview->setShowOriginal(false);

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

    if (m_current >= 0) {
        const Layer* l = activeLayer();
        const QImage adjustedOnly = ImageAdjuster::apply(
            m_images[m_current].source,
            l ? l->adjustments : Adjustments{});
        m_preview->setOriginalImage(adjustedOnly);
    }

    m_preview->setPanMode(m_spaceDown && !capsLockActive);
    updateDisplayedPreview();

    if (capsLockActive) {
        m_preview->setStatus("Original + adjustments (Caps Lock active)");
    } else if (m_spaceDown) {
        m_preview->setStatus("Pan (hold Space and drag)");
    } else if (!m_lastRender.isNull()) {
        m_preview->setStatus("Done");
    } else if (!m_lastPreviewFrame.isNull()) {
        m_preview->setStatus("Preview (full render pending…)");
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    // Backspace / Delete removes the active layer (unless typing in a field).
    if ((event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)
        && !qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
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
    l.name    = uniqueLayerName(st, kind);
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
            case LayerKind::Halftone: l.halftone = m_right->halftoneSettings(); l.halftone.tonal = tonal; break;
            case LayerKind::Dither:   l.dither   = m_right->ditherSettings();   l.dither.tonal   = tonal; break;
            case LayerKind::Ascii:    l.ascii    = m_right->asciiSettings();    l.ascii.tonal    = tonal; break;
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
    int idx = findLayerById(p.layers, p.activeLayerId);
    if (idx < 0 && !p.layers.empty()) idx = 0;
    if (idx < 0) return;

    const Layer& l = p.layers[idx];
    m_left->setAdjustments(l.adjustments);
    m_left->setFrameSize(p.frameW > 0 ? p.frameW : 1080,
                         p.frameH > 0 ? p.frameH : 1080);
    m_left->setTransform(l.transform);   // boxes follow the active layer
    pushPreviewTransform();              // overlay follows the active layer

    // Fill (tonal), Fusion and enabled-state are handled by setFromLayer;
    // background is shared and set explicitly.
    m_right->setBackground(p.background, p.backgroundOpacity);
    m_right->setFromLayer(l);
}

void MainWindow::syncLayersPanel()
{
    if (m_current < 0) return;
    SessionImage& board = m_images[m_current];
    auto& st = board.state;
    if (findLayerById(st.layers, st.activeLayerId) < 0 && !st.layers.empty())
        st.activeLayerId = st.layers[0].id;

    // Small source per media for parent + child thumbnails.
    QHash<int, QImage> mediaImages;
    for (const ParentGroup& g : st.parents) {
        const auto it = board.media.find(g.mediaId);
        if (it == board.media.end() || it->image.isNull()) continue;
        mediaImages.insert(g.mediaId,
            it->image.scaled(92, 64, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }

    m_layersPanel->setBackground(st.background, st.backgroundOpacity);
    m_layersPanel->setTree(st.parents, st.layers, st.activeLayerId, mediaImages);
    m_layersPanel->setSelection(m_selection);   // reflect multi-select highlight
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

    if (m_autoKey) {
        autoKeyChanged(before, after);
        syncTimeline();   // tracks may have appeared/changed
    }

    scheduleRender();         // centre preview updates live
    m_previewTimer.start();   // layer thumbs catch up once edits settle
    m_undoTimer.start();
}

// Auto-key: write a keyframe at the playhead for every numeric parameter
// whose value changed between the shown baseline and the new panel values.
void MainWindow::autoKeyChanged(const SessionParams& before, const SessionParams& after)
{
    if (m_current < 0) return;
    Animation& anim = m_images[m_current].anim;
    const int frame = anim.playhead;

    const double bgB = getDocParam(before, ParamId::BackgroundOpacity);
    const double bgA = getDocParam(after,  ParamId::BackgroundOpacity);
    if (bgB != bgA) upsertKey(anim, -1, ParamId::BackgroundOpacity, frame, bgA);

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
        if (getParam(lb, id) != getParam(la, id))
            upsertKey(anim, aid, id, frame, getParam(la, id));
    }
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
    scheduleRender();
}

void MainWindow::syncTimeline()
{
    m_timeline->setAnimation(m_current >= 0 ? m_images[m_current].anim : Animation{});
}

void MainWindow::onTimelineEdited()
{
    if (m_current < 0) return;
    m_playCacheValid = false;
    m_images[m_current].anim = m_timeline->animation();
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::onPlayToggled(bool playing)
{
    if (m_current < 0) { m_playing = false; return; }

    if (playing) {
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
bool MainWindow::buildPlayCache()
{
    if (m_current < 0) return false;
    if (m_playCacheValid && !m_playCache.isEmpty()) return true;

    SessionImage& img = m_images[m_current];
    const int f0 = img.anim.frameStart;
    const int f1 = qMax(f0, img.anim.frameEnd);
    const int count = f1 - f0 + 1;
    if (count <= 1 && !img.anim.hasAnimation() && img.frames.isEmpty())
        return false;   // single still, nothing to animate

    m_playCache.clear();
    m_playCache.reserve(count);

    QProgressDialog progress("Preparing playback…", "Cancel", 0, count, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    for (int i = 0; i < count; ++i) {
        progress.setValue(i);
        if (progress.wasCanceled()) { m_playCache.clear(); return false; }

        const int frame = f0 + i;
        QImage src = img.source;
        if (!img.frames.isEmpty())
            src = img.frames[qBound(0, frame - f0, img.frames.size() - 1)];
        const SessionParams p = bakeGroupVisibility(img.anim.hasAnimation()
            ? paramsAtFrame(img.state, img.anim, frame)
            : img.state);
        m_playCache.append(RenderWorker::renderPreview(src, p, RenderWorker::FAST_MAX_PX,
                                                       layerSourcesAt(img, frame)));
    }
    progress.setValue(count);
    m_playCacheValid = true;
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
    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
}

QString MainWindow::uniqueLayerName(const SessionParams& p, LayerKind kind) const
{
    const QString base = layerKindName(kind);
    int count = 0;
    for (const Layer& l : p.layers)
        if (l.kind == kind) ++count;
    if (count == 0) return base;

    int n = count + 1;
    auto exists = [&p](const QString& name) {
        for (const Layer& l : p.layers)
            if (l.name == name) return true;
        return false;
    };
    QString candidate = base + " " + QString::number(n);
    while (exists(candidate))
        candidate = base + " " + QString::number(++n);
    return candidate;
}

void MainWindow::onModeSelected(RenderMode m)
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;
    const LayerKind kind = layerKindForMode(m);

    // The mode tabs change the ACTIVE child's treatment in place, keeping its
    // parent (mediaId), transform and adjustments. The layer carries a settings
    // struct for every kind, so switching just picks which one renders.
    Layer* act = activeLayer();
    if (!act) return;
    if (act->kind == kind) { applyParams(st); return; }

    const bool wasAutoName = (act->name == layerKindName(act->kind));
    act->kind = kind;
    if (wasAutoName) act->name = uniqueLayerName(st, kind);

    applyParams(st);
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
    syncLayersPanel();
    scheduleRender();
}

// Ctrl-click a row: add/remove just that layer from the selection.
void MainWindow::onLayerToggleRequested(int layerId)
{
    if (m_current < 0) return;
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
    const bool wasAutoName = (l.name == layerKindName(l.kind));
    l.kind = LayerKind::Original;     // revert to raw; per-kind settings stay dormant
    if (wasAutoName) l.name = uniqueLayerName(st, LayerKind::Original);

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

void MainWindow::onLayerTransformChanged(const LayerTransform& t)
{
    if (m_current < 0) return;
    Layer* l = activeLayer();
    if (!l || l->transform == t) return;

    l->transform = t;
    m_playCacheValid = false;
    m_left->setTransform(t);   // keep the numeric boxes in sync (silent)
    pushPreviewTransform();
    // During a live canvas drag, only the cheap interactive pass runs (the full
    // pass is deferred to drag end) — re-rendering the whole document, and the
    // layer thumbnails, on every mouse move is what made many-layer drags chaotic.
    scheduleRender(/*previewOnly=*/m_transformDragging);
    if (!m_transformDragging) m_previewTimer.start();   // thumbs catch up after edits
    m_undoTimer.start();
}

void MainWindow::onGroupTransformChanged(const QHash<int, LayerTransform>& byId)
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;
    bool any = false;
    for (auto it = byId.cbegin(); it != byId.cend(); ++it) {
        const int idx = findLayerById(st.layers, it.key());
        if (idx >= 0 && !(st.layers[idx].transform == it.value())) {
            st.layers[idx].transform = it.value();
            any = true;
        }
    }
    if (!any) return;

    m_playCacheValid = false;
    if (const Layer* l = activeLayer()) m_left->setTransform(l->transform);
    pushPreviewTransform();
    scheduleRender(/*previewOnly=*/m_transformDragging);
    if (!m_transformDragging) m_previewTimer.start();
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
        items.push_back({ l.id, l.transform, native });
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
                                  native, frame, l != nullptr && !native.isEmpty());
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
    syncLayersPanel();
    scheduleRender();
}

void MainWindow::onAddLayerRequested()
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;

    const Layer* act = activeLayer();
    if (!act) return;                 // need a parent to attach the new child to

    Layer nl = *act;                  // duplicate the active child (settings + parent)
    nl.id      = st.nextLayerId++;
    nl.kind    = LayerKind::Original; // new children start mode-less; user picks a mode
    nl.name    = uniqueLayerName(st, nl.kind);
    nl.visible = true;
    nl.pinned  = true;

    const int insertAt = findLayerById(st.layers, act->id);
    st.layers.insert(st.layers.begin() + qMax(0, insertAt), nl);
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
    regroupLayers(m_images[m_current].state);   // keep composite order == tree order

    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
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
    for (const Layer& c : copies) st.layers.push_back(c);
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

    // Remove the source from the COMPOSITION (its layers + group), but keep it
    // in the library (board.media + filmstrip) so it can be re-added. Use the
    // filmstrip ✕ to remove it from the library entirely.
    st.layers.erase(std::remove_if(st.layers.begin(), st.layers.end(),
                    [mediaId](const Layer& l){ return l.mediaId == mediaId; }),
                    st.layers.end());
    st.parents.erase(st.parents.begin() + pi);

    if (findLayerById(st.layers, st.activeLayerId) < 0)
        st.activeLayerId = st.layers.empty() ? -1 : st.layers.front().id;
    syncBoardSource(board);
    commitStructuralChange();
}

// Supersample factor for the full pass at the current zoom. 1.0 = render at the
// native frame resolution (preview just upscales it).
float MainWindow::zoomQualityScale() const
{
    // ponytail: never supersample on zoom. Re-rendering on every scroll was
    // distracting in every mode (and for raster dither it shifted the cell
    // grid). The on-screen raster is just upscaled instead.
    return 1.0f;
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
    if (!qualityOnly) m_lastRender = {};

    const SessionImage& img = m_images[m_current];

    // Source: a clip uses the playhead's frame; a still uses its image.
    QImage source = img.source;
    if (!img.frames.isEmpty()) {
        const int fi = qBound(0, img.anim.playhead - img.anim.frameStart,
                              img.frames.size() - 1);
        source = img.frames[fi];
    }

    // Parameters: bake the animation at the current playhead, then fold each
    // parent group's master visibility into its children.
    const SessionParams params = bakeGroupVisibility(img.anim.hasAnimation()
        ? paramsAtFrame(img.state, img.anim, img.anim.playhead)
        : img.state);

    // Render the live preview at the size it's actually shown on screen, so the
    // fast pass already matches the (downscaled) final — no jarring quality jump.
    const qreal dpr = m_preview->devicePixelRatioF();
    int previewPx = qMax(qRound(m_preview->width()  * dpr),
                         qRound(m_preview->height() * dpr));
    // While dragging a layer on the canvas, render smaller so each interactive
    // pass is far cheaper (the full-res pass lands once the drag ends).
    if (m_transformDragging) previewPx = qMax(360, previewPx / 2);
    m_worker->setInteractivePreviewPx(previewPx);

    // Zoomed in → render the full pass larger so the vector symbols stay crisp
    // instead of upscaling a frame-sized raster (the worker caps the budget).
    // Skipped for dither (would change the pixel processing — see helper).
    m_worker->setFullQualityScale(zoomQualityScale());

    const QHash<int, QImage> ls = layerSourcesAt(img, img.anim.playhead);
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
    m_preview->setStatus(isPreview ? "Preview (full render pending…)" : "Done");
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
    m_playCacheValid = false;
    applyParams(img.state);
    syncLayersPanel();
    syncTimeline();
    setPlayhead(img.anim.playhead);
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
    m_playCacheValid = false;
    applyParams(img.state);
    syncLayersPanel();
    syncTimeline();
    setPlayhead(img.anim.playhead);
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
    if (!m_lastRender.isNull()) {
        QApplication::clipboard()->setImage(m_lastRender);
        m_preview->setStatus("Copied to clipboard");
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
        const auto reply = QMessageBox::question(this, "Import sequence",
            QString("These %1 files look like an image sequence.\n\n"
                    "Import them as a single animated clip?").arg(paths.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (reply == QMessageBox::Yes) { importSequence(paths); return {}; }
    }

    // Load a file into a MediaClip (decoding video via ffmpeg).
    auto loadClip = [this](const QString& path, MediaClip& clip) -> bool {
        clip.name = path.startsWith(":/") ? "example" : QFileInfo(path).fileName();
        if (isVideoFile(path)) {
            if (!VideoIO::available()) {
                QMessageBox::warning(this, "Import video",
                    "ffmpeg.exe was not found.\n\nPlace ffmpeg.exe next to the application "
                    "to import and export videos.");
                return false;
            }
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QVector<QImage> frames; double fps = 24.0; QString err;
            const bool ok = VideoIO::decode(path, frames, fps, err);
            QApplication::restoreOverrideCursor();
            if (!ok) { QMessageBox::warning(this, "Import video", err); return false; }
            clip.frames = frames; clip.image = frames.first(); clip.fps = fps;
            return true;
        }
        QImage im(path);
        if (im.isNull()) {
            QMessageBox::warning(this, "Error", "Could not load image:\n" + path);
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
    switchToImage(m_current);   // init panels/preview/timeline for the empty board
    return m_current;
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

    Layer child = makeChildLayer(st, mediaId, layerKindForMode(m_right->mode()),
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
        QMessageBox::warning(this, "Import sequence", "No valid images in the selection.");
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
        m_capsLockActive = false;
        m_spaceDown = false;
        m_preview->setPanMode(false);
        m_preview->setShowOriginal(false);
        m_preview->resetZoom();
        m_preview->setImage({});
        m_preview->setStatus("Drop images here or use the orange button below");
        m_filmstrip->setActive(-1);
        m_left->setSourceImage({});
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
    m_filmstrip->setActive(index);
    m_left->setAddLayerVisible(true);
    m_layersPanel->setVisible(true);
    m_layersPanel->setSourceImage(m_images[index].source);
    syncLayersPanel();
    m_lastRender = {};
    m_lastPreviewFrame = {};
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
        const auto reply = QMessageBox::question(this, "Remove from library",
            "This source is used by one or more layers.\n\nRemove it and its layers?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }

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
        QMessageBox::information(this, "Export", "No image loaded.");
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
        QMessageBox::critical(this, "Export Failed",
                              "Could not save file:\n" + savePath);
        return;
    }

    m_preview->setStatus("Exported: " + savePath);
}

// Export the current frame as a vector SVG (halftone/ascii/dither → shapes).
void MainWindow::exportSvg(const QString& baseName)
{
    if (m_current < 0) return;
    const SessionImage& img = m_images[m_current];

    QImage source = img.source;
    if (!img.frames.isEmpty()) {
        const int fi = qBound(0, img.anim.playhead - img.anim.frameStart, img.frames.size() - 1);
        source = img.frames[fi];
    }
    const SessionParams params = bakeGroupVisibility(img.anim.hasAnimation()
        ? paramsAtFrame(img.state, img.anim, img.anim.playhead)
        : img.state);
    const QHash<int, QImage> ls = layerSourcesAt(img, img.anim.playhead);

    // Heavy-render guard: a fine grid / small dither cell can produce hundreds of
    // thousands of shapes — a huge file that may lag or crash while writing.
    const int elements = RenderWorker::estimateSvgElements(source, params, ls);
    if (elements > 150000) {
        const auto reply = QMessageBox::warning(this, "Heavy SVG export",
            QString("This export contains roughly %1 vector shapes.\n\n"
                    "The SVG may be very large and slow to open, and the app "
                    "could lag or run out of memory while writing it.\n\nContinue?")
                .arg(elements),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }

    const QString savePath = QFileDialog::getSaveFileName(
        this, "Export SVG", baseName + ".svg", "SVG Image (*.svg)");
    if (savePath.isEmpty()) return;

    if (!RenderWorker::renderDocumentToSvg(savePath, source, params, ls)) {
        QMessageBox::critical(this, "Export Failed", "Could not write SVG:\n" + savePath);
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
    QProgressDialog progress("Rendering frames…", "Cancel", 0, count, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    int written = 0;
    for (int i = 0; i < count; ++i) {
        progress.setValue(i);
        if (progress.wasCanceled()) break;

        const int frame = f0 + i;
        QImage src = img.source;
        if (!img.frames.isEmpty()) {
            const int fi = qBound(0, frame - f0, img.frames.size() - 1);
            src = img.frames[fi];
        }
        const SessionParams p = bakeGroupVisibility(img.anim.hasAnimation()
            ? paramsAtFrame(img.state, img.anim, frame)
            : img.state);

        const QImage canvas = RenderWorker::renderDocument(src, p, layerSourcesAt(img, frame));
        const QString fn = QString("%1/%2_%3.png")
            .arg(dir, baseName, QString::number(frame).rightJustified(digits, '0'));
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
        QMessageBox::warning(this, "Export video",
            "ffmpeg.exe was not found.\n\nPlace ffmpeg.exe next to the application to export videos.");
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
        QMessageBox::critical(this, "Export video", "Could not create a temporary folder.");
        return;
    }

    QProgressDialog progress("Rendering frames…", "Cancel", 0, count, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    for (int i = 0; i < count; ++i) {
        progress.setValue(i);
        if (progress.wasCanceled()) return;

        const int frame = f0 + i;
        QImage src = img.source;
        if (!img.frames.isEmpty())
            src = img.frames[qBound(0, frame - f0, img.frames.size() - 1)];
        const SessionParams p = bakeGroupVisibility(img.anim.hasAnimation()
            ? paramsAtFrame(img.state, img.anim, frame)
            : img.state);

        QImage canvas = RenderWorker::renderDocument(src, p, layerSourcesAt(img, frame));
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

    if (!ok) { QMessageBox::critical(this, "Export video", err); return; }
    m_preview->setStatus("Exported video: " + savePath);
}
