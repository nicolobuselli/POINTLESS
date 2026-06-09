#pragma once

#include <QMainWindow>
#include <QImage>
#include <QString>

class PreviewWidget;
class ControlPanel;
class RenderWorker;

/**
 * MainWindow
 *
 * Orchestrates PreviewWidget (left) and ControlPanel (right).
 * Handles file loading, export, and wires the RenderWorker.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onChooseFile();
    void onFileDropped(const QString& path);
    void onParamsChanged();
    void onRenderComplete(QImage result, bool isPreview);
    void onExport();

private:
    void loadImage(const QString& path);
    void scheduleRender();

    PreviewWidget* m_preview;
    ControlPanel*  m_controls;
    RenderWorker*  m_worker;

    QImage  m_sourceImage;
    QString m_sourceFilePath;
    QString m_sourceFormat;
};
