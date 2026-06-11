#include "MainWindow.h"
#include "PreviewWidget.h"
#include "AdjustmentsPanel.h"
#include "ModePanel.h"
#include "FilmstripWidget.h"
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
        SessionParams p = m_images[m_current].state;
        p.adjustments = Adjustments{};
        m_images[m_current].state = p;
        applyParams(p);
        scheduleRender();
        m_undoTimer.start();
    });
    connect(m_right, &ModePanel::paramsChanged,             this, &MainWindow::onParamsChanged);

    connect(m_preview,   &PreviewWidget::filesDropped,           this, &MainWindow::onFilesDropped);
    connect(m_filmstrip, &FilmstripWidget::filesDropped,         this, &MainWindow::onFilesDropped);
    connect(m_filmstrip, &FilmstripWidget::addRequested,         this, &MainWindow::onAddRequested);
    connect(m_filmstrip, &FilmstripWidget::thumbSelected,        this, &MainWindow::onThumbSelected);
    connect(m_filmstrip, &FilmstripWidget::thumbCloseRequested,  this, &MainWindow::onThumbCloseRequested);

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
            if (!keyEvent->isAutoRepeat()) {
                if (keyEvent->key() == Qt::Key_Space || keyEvent->key() == Qt::Key_Shift) {
                    event->accept();
                    return true;
                }
            }
        }

        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat()) {
                if (keyEvent->key() == Qt::Key_Space) {
                    m_spaceDown = true;
                    updatePreviewInteractionState();
                    event->accept();
                    return true;
                }
                if (keyEvent->key() == Qt::Key_Shift) {
                    m_shiftDown = true;
                    updatePreviewInteractionState();
                    event->accept();
                    return true;
                }
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
                if (keyEvent->key() == Qt::Key_Shift) {
                    m_shiftDown = false;
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

    if (m_showOriginalWhileSpace) {
        const QImage adjustedOnly = ImageAdjuster::apply(
            m_images[m_current].source,
            m_images[m_current].state.adjustments);
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
    m_showOriginalWhileSpace = m_shiftDown;

    if (m_current >= 0) {
        const QImage adjustedOnly = ImageAdjuster::apply(
            m_images[m_current].source,
            m_images[m_current].state.adjustments);
        m_preview->setOriginalImage(adjustedOnly);
    }

    m_preview->setPanMode(m_spaceDown && !m_shiftDown);
    updateDisplayedPreview();

    if (m_shiftDown) {
        m_preview->setStatus("Original + adjustments (hold Shift)");
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

SessionParams MainWindow::collectParams() const
{
    SessionParams p;
    p.adjustments       = m_left->adjustments();
    p.mode              = m_right->mode();
    p.halftone          = m_right->halftoneSettings();
    p.dither            = m_right->ditherSettings();
    p.ascii             = m_right->asciiSettings();
    p.background        = m_right->background();
    p.backgroundOpacity = m_right->backgroundOpacity();
    return p;
}

void MainWindow::applyParams(const SessionParams& p)
{
    m_left->setAdjustments(p.adjustments);
    m_right->setAll(p);
}

void MainWindow::onParamsChanged()
{
    if (m_current < 0) return;
    m_images[m_current].state = collectParams();
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
        m_preview->setOriginalImage(ImageAdjuster::apply(
            m_images[m_current].source,
            m_images[m_current].state.adjustments));
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
        m_showOriginalWhileSpace = false;
        m_spaceDown = false;
        m_shiftDown = false;
        m_preview->setPanMode(false);
        m_preview->setShowOriginal(false);
        m_preview->resetZoom();
        m_preview->setImage({});
        m_preview->setStatus("Drop images here or use the orange button below");
        m_filmstrip->setActive(-1);
        return;
    }
    m_current = index;
    applyParams(m_images[index].state);
    m_filmstrip->setActive(index);
    m_lastRender = {};
    m_lastPreviewFrame = {};
    m_spaceDown = false;
    m_shiftDown = false;
    m_showOriginalWhileSpace = false;
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
        const QImage adjusted = ImageAdjuster::apply(source, params.adjustments);

        QSvgGenerator gen;
        gen.setFileName(savePath);
        gen.setSize(adjusted.size());
        gen.setViewBox(QRect(QPoint(0, 0), adjusted.size()));
        gen.setTitle("ULTRA_Ditherer export");

        QPainter painter(&gen);
        if (params.backgroundOpacity > 0.001f) {
            QColor bg = params.background;
            bg.setAlphaF(params.backgroundOpacity);
            painter.fillRect(QRect(QPoint(0, 0), adjusted.size()), bg);
        }
        RenderWorker::renderModeInto(painter, adjusted, params);
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
