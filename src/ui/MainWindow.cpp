#include "MainWindow.h"
#include "PreviewWidget.h"
#include "AdjustmentsPanel.h"
#include "ModePanel.h"
#include "FilmstripWidget.h"
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
#include <QShortcut>
#include <QSvgGenerator>

namespace {
constexpr int kMaxUndoSteps   = 100;
constexpr int kUndoDebounceMs = 400;

QFrame* makeVSeparator()
{
    auto* f = new QFrame;
    f->setObjectName("vseparator");
    f->setFrameShape(QFrame::NoFrame);
    f->setFixedWidth(1);
    return f;
}
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

    m_left = new AdjustmentsPanel;

    auto* centerWidget = new QWidget;
    auto* cv = new QVBoxLayout(centerWidget);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    m_preview   = new PreviewWidget;
    m_filmstrip = new FilmstripWidget;
    cv->addWidget(m_preview, 1);
    cv->addWidget(m_filmstrip);

    m_layersPanel = new LayersPanel(m_preview);

    m_right = new ModePanel;

    hl->addWidget(m_left);
    hl->addWidget(makeVSeparator());
    hl->addWidget(centerWidget, 1);
    hl->addWidget(makeVSeparator());
    hl->addWidget(m_right);

    setCentralWidget(central);

    qApp->installEventFilter(this);

    // ── Signals ──────────────────────────────────────────────
    connect(m_left,  &AdjustmentsPanel::adjustmentsChanged, this, &MainWindow::onParamsChanged);
    connect(m_left,  &AdjustmentsPanel::exportRequested,    this, &MainWindow::onExport);
    connect(m_left,  &AdjustmentsPanel::resetRequested,     this, [this]() {
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

    connect(m_preview,   &PreviewWidget::filesDropped,           this, &MainWindow::onFilesDropped);
    connect(m_filmstrip, &FilmstripWidget::filesDropped,         this, &MainWindow::onFilesDropped);
    connect(m_filmstrip, &FilmstripWidget::addRequested,         this, &MainWindow::onAddRequested);
    connect(m_filmstrip, &FilmstripWidget::thumbSelected,        this, &MainWindow::onThumbSelected);
    connect(m_filmstrip, &FilmstripWidget::thumbCloseRequested,  this, &MainWindow::onThumbCloseRequested);

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
        switch (l.kind) {
            case LayerKind::Halftone: l.halftone = m_right->halftoneSettings(); break;
            case LayerKind::Dither:   l.dither   = m_right->ditherSettings();   break;
            case LayerKind::Ascii:    l.ascii    = m_right->asciiSettings();    break;
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
    m_right->setFromLayer(l, p.background, p.backgroundOpacity);
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
    m_images[m_current].state = collectParams();
    syncLayersPanel();   // row thumbs follow the layer's adjustments
    scheduleRender();
    m_undoTimer.start();
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

void MainWindow::scheduleRender()
{
    if (m_current < 0) return;
    m_worker->requestRender(m_images[m_current].source, m_images[m_current].state);
}

void MainWindow::onRenderComplete(QImage result, bool isPreview)
{
    if (isPreview) {
        m_lastPreviewFrame = result;
    } else {
        m_lastRender = result;
    }
    if (m_current >= 0) {
        const Layer* l = activeLayer();
        m_preview->setOriginalImage(ImageAdjuster::apply(
            m_images[m_current].source,
            l ? l->adjustments : Adjustments{}));
    }
    updateDisplayedPreview();
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
        && img.undoStack[img.undoIndex] == img.state)
        return;   // nothing actually changed

    // Drop redo branch
    while (img.undoStack.size() > img.undoIndex + 1)
        img.undoStack.removeLast();

    img.undoStack.append(img.state);
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
    img.state = img.undoStack[img.undoIndex];
    applyParams(img.state);
    syncLayersPanel();
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
    img.state = img.undoStack[img.undoIndex];
    applyParams(img.state);
    syncLayersPanel();
    scheduleRender();
}

void MainWindow::copyToClipboard()
{
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
        si.undoStack.append(si.state);
        si.undoIndex = 0;

        m_images.append(si);
        m_filmstrip->addThumb(img, si.name);
        lastAdded = m_images.size() - 1;
    }
    if (lastAdded >= 0) switchToImage(lastAdded);
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
        m_layersPanel->setVisible(false);
        return;
    }
    m_current = index;
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
    scheduleRender();
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

    QString format = m_left->outputFormat().toLower();
    QString name   = m_left->outputFileName();
    if (name.isEmpty()) name = "output";

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
