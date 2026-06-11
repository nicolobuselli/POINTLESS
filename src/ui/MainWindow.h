#pragma once

#include <QMainWindow>
#include <QImage>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QKeyEvent>
#include "../core/Params.h"

class PreviewWidget;
class AdjustmentsPanel;
class ModePanel;
class FilmstripWidget;
class RenderWorker;

/**
 * MainWindow
 *
 * Layout: AdjustmentsPanel | (PreviewWidget over FilmstripWidget) | ModePanel
 *
 * Owns the multi-image session: every loaded image keeps its own full
 * parameter state and undo history. Shortcuts: Ctrl+Z / Ctrl+Shift+Z
 * (undo/redo), Ctrl+C (copy rendered image).
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onParamsChanged();
    void onRenderComplete(QImage result, bool isPreview);
    void onExport();
    void onAddRequested();
    void onFilesDropped(const QStringList& paths);
    void onThumbSelected(int index);
    void onThumbCloseRequested(int index);
    void undo();
    void redo();
    void copyToClipboard();

private:
    struct SessionImage {
        QString                name;
        QImage                 source;
        SessionParams          state;
        QVector<SessionParams> undoStack;
        int                    undoIndex = -1;
    };

    SessionParams collectParams() const;
    void applyParams(const SessionParams& p);
    void scheduleRender();
    void pushUndoSnapshot();
    void addImages(const QStringList& paths);
    void switchToImage(int index);
    void updateDisplayedPreview();
    void updatePreviewInteractionState();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    AdjustmentsPanel* m_left      = nullptr;
    ModePanel*        m_right     = nullptr;
    PreviewWidget*    m_preview   = nullptr;
    FilmstripWidget*  m_filmstrip = nullptr;
    RenderWorker*     m_worker    = nullptr;

    QVector<SessionImage> m_images;
    int                   m_current = -1;

    QImage m_lastRender;
    QImage m_lastPreviewFrame;
    bool   m_showOriginalWhileSpace = false;
    bool   m_spaceDown = false;
    bool   m_shiftDown = false;
    QTimer m_undoTimer;
};
