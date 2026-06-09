#include "MainWindow.h"
#include "PreviewWidget.h"
#include "ControlPanel.h"
#include "../workers/RenderWorker.h"
#include "../core/HalftoneRenderer.h"

#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QFileInfo>
#include <QSvgGenerator>
#include <QPainter>
#include <QApplication>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_worker(new RenderWorker(this))
{
    setWindowTitle("ULTRA TOOL — Ditherer");
    setWindowIcon(QIcon(":\/logo.png"));
    setMinimumSize(960, 600);
    resize(1280, 800);

    // Central splitter
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(2);

    m_preview  = new PreviewWidget(splitter);
    m_controls = new ControlPanel(splitter);

    splitter->addWidget(m_preview);
    splitter->addWidget(m_controls);
    splitter->setStretchFactor(0, 7);   // preview 70%
    splitter->setStretchFactor(1, 3);   // controls 30%
    splitter->setSizes({896, 384});

    setCentralWidget(splitter);
    statusBar()->showMessage("Ready");

    // Connections
    connect(m_controls,  &ControlPanel::fileRequested,   this, &MainWindow::onChooseFile);
    connect(m_controls,  &ControlPanel::paramsChanged,   this, &MainWindow::onParamsChanged);
    connect(m_controls,  &ControlPanel::exportRequested, this, &MainWindow::onExport);
    connect(m_preview,   &PreviewWidget::fileDropped,    this, &MainWindow::onFileDropped);
    connect(m_worker, &RenderWorker::renderComplete, this, &MainWindow::onRenderComplete);
    connect(m_worker, &RenderWorker::renderStarted, this, [this](bool isPreview) {
        statusBar()->showMessage(isPreview ? "Preview…" : "Rendering full resolution…");
    });
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// File loading
// ---------------------------------------------------------------------------

void MainWindow::onChooseFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Open Image", "",
        "Images (*.png *.jpg *.jpeg *.bmp);;All Files (*)");
    if (!path.isEmpty()) loadImage(path);
}

void MainWindow::onFileDropped(const QString& path)
{
    loadImage(path);
}

void MainWindow::loadImage(const QString& path)
{
    QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, "Error", "Could not load image:\n" + path);
        return;
    }

    m_sourceImage    = img;
    m_sourceFilePath = path;
    m_sourceFormat   = QFileInfo(path).suffix().toLower();

    // Show filename in control panel label
    // (We access via findChild since ControlPanel owns the label)
    if (auto* lbl = m_controls->findChild<QLabel*>("lblFilePath"))
        lbl->setText(QFileInfo(path).fileName());

    statusBar()->showMessage("Loaded: " + QFileInfo(path).fileName());
    scheduleRender();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void MainWindow::onParamsChanged()
{
    scheduleRender();
}

void MainWindow::scheduleRender()
{
    if (m_sourceImage.isNull()) return;
    m_worker->requestRender(m_sourceImage, m_controls->currentParams());
}

void MainWindow::onRenderComplete(QImage result, bool isPreview)
{
    m_preview->setImage(result);
    statusBar()->showMessage(isPreview ? "Preview (full render pending…)" : "Done");
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

void MainWindow::onExport()
{
    if (m_sourceImage.isNull()) {
        QMessageBox::information(this, "Export", "No image loaded.");
        return;
    }

    QString format = m_controls->outputFormat().toLower();
    QString name   = m_controls->outputFileName();
    if (name.isEmpty()) name = "output";

    QString filter;
    if (format == "png")      filter = "PNG Image (*.png)";
    else if (format == "jpg") filter = "JPEG Image (*.jpg)";
    else if (format == "svg") filter = "SVG File (*.svg)";

    QString savePath = QFileDialog::getSaveFileName(
        this, "Export", name + "." + format, filter);
    if (savePath.isEmpty()) return;

    if (format == "svg") {
        // Render to SVG via QSvgGenerator
        QSvgGenerator gen;
        gen.setFileName(savePath);
        gen.setSize(m_sourceImage.size());
        gen.setViewBox(QRect(QPoint(0, 0), m_sourceImage.size()));
        gen.setTitle("ULTRA_Ditherer export");

        QPainter painter(&gen);
        painter.fillRect(QRect(QPoint(0,0), m_sourceImage.size()), Qt::white);

        auto params = m_controls->currentParams();
        HalftoneRenderer renderer;
        renderer.render(m_sourceImage, painter, params);
        painter.end();
    } else {
        // Render to QImage and save
        QImage canvas(m_sourceImage.size(), QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::white);
        QPainter painter(&canvas);

        auto params = m_controls->currentParams();
        HalftoneRenderer renderer;
        renderer.render(m_sourceImage, painter, params);
        painter.end();

        int quality = (format == "jpg") ? 95 : -1;
        if (!canvas.save(savePath, format.toUpper().toUtf8().constData(), quality)) {
            QMessageBox::critical(this, "Export Failed",
                                  "Could not save file:\n" + savePath);
            return;
        }
    }

    statusBar()->showMessage("Exported: " + savePath);
    QMessageBox::information(this, "Export", "Saved to:\n" + savePath);
}
