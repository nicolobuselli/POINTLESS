#include "MainWindow.h"
#include "PreviewWidget.h"
#include "ControlsPanel.h"
#include "ModePanel.h"
#include "FilmstripWidget.h"
#include "TimelineWidget.h"
#include "LayersPanel.h"
#include "Widgets.h"
#include "../workers/RenderWorker.h"
#include "../core/ImageAdjuster.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QSvgGenerator>

namespace {
constexpr int kMaxUndoSteps   = 100;
constexpr int kUndoDebounceMs = 400;
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_worker(new RenderWorker(this))
{
    setWindowTitle("ULTRA TOOL — Ditherer");
    setWindowIcon(QIcon(":/logo.png"));
    setMinimumSize(1100, 680);

    // ── Layout ───────────────────────────────────────────────
    auto* central = new QWidget;
    auto* hl = new QHBoxLayout(central);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);

    m_left = new ControlsPanel;

    m_preview   = new PreviewWidget;
    m_filmstrip = new FilmstripWidget;
    m_timeline  = new TimelineWidget;
    m_preview->setMinimumHeight(160);   // keep panes from collapsing to nothing

    // Bottom panel: "Images | Timeline" tabs over a stacked area.
    auto* bottomPanel = new QWidget;
    bottomPanel->setMinimumHeight(130);   // never collapses away
    auto* bp = new QVBoxLayout(bottomPanel);
    bp->setContentsMargins(0, 0, 0, 0);
    bp->setSpacing(0);

    auto* tabRow = new QWidget;
    tabRow->setObjectName("tabRow");
    auto* trl = new QHBoxLayout(tabRow);
    trl->setContentsMargins(8, 4, 0, 0);
    trl->setSpacing(4);
    auto* tabImages   = new QPushButton("Images");
    auto* tabTimeline = new QPushButton("Timeline");
    for (QPushButton* b : { tabImages, tabTimeline }) {
        b->setObjectName("tabBtn");
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFixedHeight(30);
        b->setMinimumWidth(96);
        b->setCursor(Qt::PointingHandCursor);
    }
    tabImages->setChecked(true);
    trl->addWidget(tabImages);
    trl->addWidget(tabTimeline);
    trl->addStretch(1);

    auto* bottomStack = new QStackedWidget;
    bottomStack->addWidget(m_filmstrip);   // page 0
    bottomStack->addWidget(m_timeline);    // page 1
    connect(tabImages,   &QPushButton::clicked, this, [bottomStack] { bottomStack->setCurrentIndex(0); });
    connect(tabTimeline, &QPushButton::clicked, this, [bottomStack] { bottomStack->setCurrentIndex(1); });
    bottomStack->setCurrentIndex(0);   // default to Images

    bp->addWidget(tabRow);
    bp->addWidget(bottomStack, 1);

    // Center column: preview over the bottom panel, vertically resizable.
    auto* centerSplit = new QSplitter(Qt::Vertical);
    centerSplit->setChildrenCollapsible(false);
    centerSplit->setHandleWidth(5);
    centerSplit->addWidget(m_preview);
    centerSplit->addWidget(bottomPanel);
    centerSplit->setStretchFactor(0, 1);
    centerSplit->setStretchFactor(1, 0);
    centerSplit->setSizes({ 600, 220 });

    m_layersPanel = new LayersPanel(m_preview);
    m_right = new ModePanel;

    // Main columns: left | center | right, horizontally resizable.
    auto* mainSplit = new QSplitter(Qt::Horizontal);
    mainSplit->setChildrenCollapsible(false);
    mainSplit->setHandleWidth(5);
    mainSplit->addWidget(m_left);
    mainSplit->addWidget(centerSplit);
    mainSplit->addWidget(m_right);
    mainSplit->setStretchFactor(0, 0);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setStretchFactor(2, 0);
    mainSplit->setSizes({ 340, 800, 340 });

    hl->addWidget(mainSplit);

    setCentralWidget(central);

    qApp->installEventFilter(this);

    // ── Signals ──────────────────────────────────────────────
    connect(m_left,  &ControlsPanel::adjustmentsChanged, this, &MainWindow::onParamsChanged);
    connect(m_left,  &ControlsPanel::tonalChanged,       this, &MainWindow::onParamsChanged);
    connect(m_left,  &ControlsPanel::backgroundChanged,  this, &MainWindow::onParamsChanged);
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
    connect(m_right, &ModePanel::paramsChanged,             this, &MainWindow::onParamsChanged);
    connect(m_right, &ModePanel::modeSelected,              this, &MainWindow::onModeSelected);
    connect(m_right, &ModePanel::exportRequested,           this, &MainWindow::onExport);

    connect(m_preview,   &PreviewWidget::filesDropped,           this, &MainWindow::onFilesDropped);
    connect(m_filmstrip, &FilmstripWidget::filesDropped,         this, &MainWindow::onFilesDropped);
    connect(m_filmstrip, &FilmstripWidget::addRequested,         this, &MainWindow::onAddRequested);
    connect(m_filmstrip, &FilmstripWidget::thumbSelected,        this, &MainWindow::onThumbSelected);
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
    // Playback advances ONLY after each preview render completes (gated in
    // onRenderComplete) so it never floods the thread and can always be stopped.
    m_playTimer.setSingleShot(true);
    m_playTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playTimer, &QTimer::timeout, this, [this]() {
        if (!m_playing || m_current < 0) return;
        Animation& a = m_images[m_current].anim;
        int next = a.playhead + 1;
        if (next > a.frameEnd) next = a.frameStart;   // loop
        a.playhead = next;
        m_timeline->setPlayheadSilent(next);
        playStep();
    });

    connect(m_layersPanel, &LayersPanel::visibilityToggled, this, &MainWindow::onLayerVisibilityToggled);
    connect(m_layersPanel, &LayersPanel::layerSelected,     this, &MainWindow::onLayerSelected);
    connect(m_layersPanel, &LayersPanel::layerRenamed,      this, &MainWindow::onLayerRenamed);
    connect(m_layersPanel, &LayersPanel::deleteRequested,   this, &MainWindow::onLayerDeleteRequested);
    connect(m_layersPanel, &LayersPanel::blendModeChanged,  this, &MainWindow::onLayerBlendChanged);
    connect(m_layersPanel, &LayersPanel::addLayerRequested, this, &MainWindow::onAddLayerRequested);
    connect(m_layersPanel, &LayersPanel::reorderRequested,  this, &MainWindow::onLayerReordered);

    connect(m_worker, &RenderWorker::renderComplete, this, &MainWindow::onRenderComplete);
    connect(m_worker, &RenderWorker::renderStarted, this, [this](bool isPreview) {
        m_preview->setStatus(isPreview ? "Preview…" : "Rendering full resolution…");
    });

    // ── Undo debounce ────────────────────────────────────────
    m_undoTimer.setSingleShot(true);
    m_undoTimer.setInterval(kUndoDebounceMs);
    connect(&m_undoTimer, &QTimer::timeout, this, &MainWindow::pushUndoSnapshot);

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
    addImages({ ":/example.jpg" });
}

MainWindow::~MainWindow() = default;

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
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

    // During playback only preview frames are produced; always show the latest
    // one so the animation is visible (the full render is stale while playing).
    if (m_playing && !m_lastPreviewFrame.isNull()) {
        m_preview->setImage(m_lastPreviewFrame);
        return;
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

// Panels → state: writes the panel values into the active layer.
SessionParams MainWindow::collectParams() const
{
    SessionParams p = (m_current >= 0) ? m_images[m_current].state : SessionParams{};

    const int idx = findLayerById(p.layers, p.activeLayerId);
    if (idx >= 0) {
        Layer& l = p.layers[idx];
        l.adjustments = m_left->adjustments();
        // Colors live in the left Colors tab now; inject them into the
        // render struct of the active layer's kind.
        const TonalSettings tonal = m_left->tonalSettings();
        switch (l.kind) {
            case LayerKind::Halftone: l.halftone = m_right->halftoneSettings(); l.halftone.tonal = tonal; break;
            case LayerKind::Dither:   l.dither   = m_right->ditherSettings();   l.dither.tonal   = tonal; break;
            case LayerKind::Ascii:    l.ascii    = m_right->asciiSettings();    l.ascii.tonal    = tonal; break;
            case LayerKind::Original: break;   // only adjustments apply
        }
    }

    p.background        = m_left->background();
    p.backgroundOpacity = m_left->backgroundOpacity();
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

    // Mirror the active layer's tonal settings into the Colors tab
    // (Original has no render settings — disable the tab).
    switch (l.kind) {
        case LayerKind::Halftone: m_left->setTonalSettings(l.halftone.tonal); break;
        case LayerKind::Dither:   m_left->setTonalSettings(l.dither.tonal);   break;
        case LayerKind::Ascii:    m_left->setTonalSettings(l.ascii.tonal);    break;
        case LayerKind::Original: break;
    }
    m_left->setColorsEnabled(l.kind != LayerKind::Original);
    m_left->setBackground(p.background, p.backgroundOpacity);

    m_right->setFromLayer(l);
}

void MainWindow::syncLayersPanel()
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;
    if (findLayerById(st.layers, st.activeLayerId) < 0 && !st.layers.empty())
        st.activeLayerId = st.layers[0].id;
    m_layersPanel->setBackground(st.background, st.backgroundOpacity);
    m_layersPanel->setLayers(st.layers, st.activeLayerId);
}

void MainWindow::onParamsChanged()
{
    if (m_current < 0) return;
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

    syncLayersPanel();   // row thumbs follow the layer's adjustments
    scheduleRender();
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

    for (const Layer& la : after.layers) {
        const int bi = findLayerById(before.layers, la.id);
        if (bi < 0) continue;
        const Layer& lb = before.layers[size_t(bi)];
        for (ParamId id : animatableParams(la)) {
            if (getParam(lb, id) != getParam(la, id))
                upsertKey(anim, la.id, id, frame, getParam(la, id));
        }
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
    m_images[m_current].anim = m_timeline->animation();
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::onPlayToggled(bool playing)
{
    if (m_current < 0) { m_playing = false; return; }
    m_playing = playing;
    if (playing) {
        m_lastRender = {};      // drop the stale full render so previews show through
        playStep();             // render current frame; chain continues on completion
    } else {
        m_playTimer.stop();
        scheduleRender();       // full-resolution render of the frame we stopped on
    }
}

void MainWindow::playStep()
{
    if (!m_playing || m_current < 0) return;
    m_playClock.restart();
    scheduleRender(/*previewOnly=*/true);
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

    const Layer* act = activeLayer();
    if (act && act->kind == kind) {
        applyParams(st);   // restore tab checked state
        return;
    }

    // Topmost existing layer of that kind, or a brand new one.
    int idx = -1;
    for (int i = 0; i < int(st.layers.size()); ++i)
        if (st.layers[i].kind == kind) { idx = i; break; }

    if (idx < 0) {
        Layer nl;
        nl.kind = kind;
        nl.id   = st.nextLayerId++;
        nl.name = uniqueLayerName(st, kind);
        st.layers.insert(st.layers.begin(), nl);
        idx = 0;
    }

    selectLayerInternal(st.layers[idx].id, true);
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
    if (m_images[m_current].state.activeLayerId == layerId) return;
    selectLayerInternal(layerId, true);
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
    if (idx < 0 || st.layers[idx].kind == LayerKind::Original) return;

    const bool wasActive = (st.activeLayerId == layerId);
    st.layers.erase(st.layers.begin() + idx);

    if (wasActive && !st.layers.empty()) {
        // Follow the topmost remaining mode layer; fall back to the top.
        int ni = 0;
        for (int i = 0; i < int(st.layers.size()); ++i)
            if (st.layers[i].kind != LayerKind::Original) { ni = i; break; }
        selectLayerInternal(st.layers[ni].id, true);
        return;
    }

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

void MainWindow::onAddLayerRequested()
{
    if (m_current < 0) return;
    auto& st = m_images[m_current].state;

    const Layer* act = activeLayer();
    Layer nl;
    int insertAt = 0;
    if (act && act->kind != LayerKind::Original) {
        nl = *act;   // duplicate the selected layer, settings included
        insertAt = findLayerById(st.layers, act->id);
    } else {
        nl.kind = layerKindForMode(m_right->mode());
    }
    nl.id      = st.nextLayerId++;
    nl.name    = uniqueLayerName(st, nl.kind);
    nl.visible = true;
    nl.pinned  = false;

    st.layers.insert(st.layers.begin() + qMax(0, insertAt), nl);
    st.activeLayerId = nl.id;

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

    syncLayersPanel();
    scheduleRender();
    m_undoTimer.start();
}

void MainWindow::scheduleRender(bool previewOnly)
{
    if (m_current < 0) return;
    const SessionImage& img = m_images[m_current];

    // Source: a clip uses the playhead's frame; a still uses its image.
    QImage source = img.source;
    if (!img.frames.isEmpty()) {
        const int fi = qBound(0, img.anim.playhead - img.anim.frameStart,
                              img.frames.size() - 1);
        source = img.frames[fi];
    }

    // Parameters: bake the animation at the current playhead.
    const SessionParams params = img.anim.hasAnimation()
        ? paramsAtFrame(img.state, img.anim, img.anim.playhead)
        : img.state;

    m_worker->requestRender(source, params, /*fullPass=*/!previewOnly);
}

void MainWindow::onRenderComplete(QImage result, bool isPreview)
{
    if (isPreview) {
        m_lastPreviewFrame = result;
    } else {
        m_lastRender = result;
    }
    // The "hold to compare original" image is not needed while playing back.
    if (m_current >= 0 && !m_playing) {
        const Layer* l = activeLayer();
        m_preview->setOriginalImage(ImageAdjuster::apply(
            m_images[m_current].source,
            l ? l->adjustments : Adjustments{}));
    }
    updateDisplayedPreview();

    if (m_playing && isPreview && m_current >= 0) {
        // Pace the next frame so playback approaches real time when renders are
        // fast and gracefully slows (never floods) when they are slow.
        const int fps  = qMax(1, m_images[m_current].anim.fps);
        const int wait = qMax(1, 1000 / fps - int(m_playClock.elapsed()));
        m_playTimer.start(wait);
        m_preview->setStatus(QString("Playing · frame %1").arg(m_images[m_current].anim.playhead));
    } else {
        m_preview->setStatus(isPreview ? "Preview (full render pending…)" : "Done");
    }
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

void MainWindow::addImages(const QStringList& paths)
{
    int lastAdded = -1;
    for (const QString& path : paths) {
        QImage img(path);
        if (img.isNull()) {
            QMessageBox::warning(this, "Error", "Could not load image:\n" + path);
            continue;
        }

        SessionImage si;
        si.name   = path.startsWith(":/") ? "example" : QFileInfo(path).fileName();
        si.source = img;
        si.state  = (m_current >= 0) ? collectParams() : SessionParams{};
        si.undoStack.append({ si.state, si.anim });
        si.undoIndex = 0;

        m_images.append(si);
        m_filmstrip->addThumb(img, si.name);
        lastAdded = m_images.size() - 1;
    }
    if (lastAdded >= 0) switchToImage(lastAdded);
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

    SessionImage si;
    si.name   = QString("sequence (%1)").arg(frames.size());
    si.frames = frames;
    si.source = frames.first();
    si.state  = (m_current >= 0) ? collectParams() : SessionParams{};
    si.anim.frameStart = 1;
    si.anim.frameEnd   = frames.size();
    si.anim.playhead   = 1;
    si.undoStack.append({ si.state, si.anim });
    si.undoIndex = 0;

    m_images.append(si);
    m_filmstrip->addThumb(si.source, si.name);
    switchToImage(m_images.size() - 1);
}

void MainWindow::switchToImage(int index)
{
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
        m_playTimer.stop();
        m_playing = false;
        m_timeline->setAnimation(Animation{});
        m_layersPanel->setVisible(false);
        return;
    }
    m_current = index;
    m_playTimer.stop();
    m_playing = false;
    m_left->setSourceImage(m_images[index].source);
    applyParams(m_images[index].state);
    m_filmstrip->setActive(index);
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
        this, "Add Images", "",
        "Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.tif *.tiff);;All Files (*)");
    if (!paths.isEmpty()) addImages(paths);
}

void MainWindow::onFilesDropped(const QStringList& paths)
{
    addImages(paths);
}

void MainWindow::onThumbSelected(int index)
{
    if (index == m_current) return;
    switchToImage(index);
}

void MainWindow::onThumbCloseRequested(int index)
{
    if (index < 0 || index >= m_images.size()) return;

    const auto reply = QMessageBox::question(
        this,
        "Are you sure?",
        "All the progress will be lost",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    m_filmstrip->removeThumb(index);
    m_images.removeAt(index);

    if (m_images.isEmpty()) {
        switchToImage(-1);
    } else if (index == m_current) {
        switchToImage(qMin(index, m_images.size() - 1));
    } else {
        if (index < m_current) --m_current;
        m_filmstrip->setActive(m_current);
    }
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
    QString name   = m_right->outputFileName();
    if (name.isEmpty()) name = "output";

    if (format == "png sequence") {
        exportSequence(name);
        return;
    }

    QString filter;
    if      (format == "png") filter = "PNG Image (*.png)";
    else if (format == "jpg") filter = "JPEG Image (*.jpg)";
    else if (format == "svg") filter = "SVG File (*.svg)";

    QString savePath = QFileDialog::getSaveFileName(
        this, "Export", name + "." + format, filter);
    if (savePath.isEmpty()) return;

    if (format == "svg") {
        const QSize outSize = source.size();

        QSvgGenerator gen;
        gen.setFileName(savePath);
        gen.setSize(outSize);
        gen.setViewBox(QRect(QPoint(0, 0), outSize));
        gen.setTitle("ULTRA_Ditherer export");

        QPainter painter(&gen);
        if (params.backgroundOpacity > 0.001f) {
            QColor bg = params.background;
            bg.setAlphaF(params.backgroundOpacity);
            painter.fillRect(QRect(QPoint(0, 0), outSize), bg);
        }
        // Visible layers bottom→top, each from its own reference image.
        // SVG cannot carry the raster blend modes, so layers are painted
        // with normal compositing here.
        for (auto it = params.layers.rbegin(); it != params.layers.rend(); ++it) {
            if (!it->visible) continue;
            const QImage adjusted = ImageAdjuster::apply(source, it->adjustments);
            painter.save();
            if (adjusted.size() != outSize && !adjusted.isNull()) {
                painter.scale(qreal(outSize.width())  / adjusted.width(),
                              qreal(outSize.height()) / adjusted.height());
            }
            RenderWorker::renderLayerInto(painter, adjusted, *it);
            painter.restore();
        }
        painter.end();
    } else {
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
        const SessionParams p = img.anim.hasAnimation()
            ? paramsAtFrame(img.state, img.anim, frame)
            : img.state;

        const QImage canvas = RenderWorker::renderDocument(src, p);
        const QString fn = QString("%1/%2_%3.png")
            .arg(dir, baseName, QString::number(frame).rightJustified(digits, '0'));
        if (canvas.save(fn, "PNG")) ++written;
    }
    progress.setValue(count);

    m_preview->setStatus(QString("Exported %1 frames to %2").arg(written).arg(dir));
}
