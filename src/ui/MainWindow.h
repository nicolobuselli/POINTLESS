#pragma once

#include <QMainWindow>
#include <QImage>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QKeyEvent>
#include "../core/Params.h"
#include "../core/Animation.h"

class PreviewWidget;
class ControlsPanel;
class ModePanel;
class FilmstripWidget;
class TimelineWidget;
class LayersPanel;
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
    void onFilesDroppedAsLayer(const QStringList& paths);
    void onThumbSelected(int mediaId);        // library: single click → highlight
    void onThumbActivated(int mediaId);       // library: double click → add as layer
    void onThumbCloseRequested(int mediaId);  // library: ✕ → remove source
    void onMediaDroppedAsLayer(int mediaId);  // library thumb dropped on layers/canvas
    void onModeSelected(RenderMode m);
    void onLayerVisibilityToggled(int layerId, bool visible);
    void onLayerSelected(int layerId);
    void onLayerRenamed(int layerId, const QString& name);
    void onLayerDeleteRequested(int layerId);
    void onLayerBlendChanged(int layerId, BlendMode mode);
    void onLayerTransformChanged(const LayerTransform& t);
    void onCanvasSelectionChanged(const QSet<int>& ids, int activeId);
    void onAddLayerRequested();
    void onLayerReordered(int layerId, int insertIndex);
    // Cascade (parent/child) operations driven by the layer tree.
    void onAddChildRequested(int mediaId);
    void onParentReordered(int mediaId, int insertIndex);
    void onGroupVisibilityToggled(int mediaId, bool visible);
    void onCollapseToggled(int mediaId, bool collapsed);
    void onDuplicateParentRequested(int mediaId);
    void onDeleteParentRequested(int mediaId);
    void onParentRenamed(int mediaId, const QString& name);
    void undo();
    void redo();
    void copyToClipboard();

private:
    // A piece of media in the board's library: a still image or a video clip.
    struct MediaClip {
        QString         name;
        QImage          image;     // still, or first frame of a clip
        QVector<QImage> frames;    // non-empty → video clip
        double          fps = 0.0;
    };

    struct UndoState { SessionParams params; Animation anim; };
    struct SessionImage {
        QString             name;          // file/identifier (filmstrip)
        QString             title;         // user-editable title shown top-left
        QImage              source;       // document base (first media's image)
        QVector<QImage>     frames;        // legacy single-clip frames (base)
        QHash<int, MediaClip> media;       // library: layers reference these by mediaId
        int                 nextMediaId = 1;
        SessionParams       state;         // un-animated baseline parameters
        Animation           anim;          // keyframe animation over the parameters
        QVector<UndoState>  undoStack;
        int                 undoIndex = -1;
    };

    SessionParams collectParams() const;
    void applyParams(const SessionParams& p);
    Layer* activeLayer();
    const Layer* activeLayer() const;

    // Cascade helpers. A parent = a media entry + a ParentGroup; its children are
    // the layers whose mediaId matches. Children render, parents don't.
    int   addParentMedia(SessionImage& board, const MediaClip& clip);   // → mediaId
    Layer makeChildLayer(SessionParams& st, int mediaId, LayerKind kind,
                         QSize native) const;
    void  regroupLayers(SessionParams& st) const;       // keep layers grouped by parent order
    void  syncBoardSource(SessionImage& board) const;   // board.source = first parent's image
    bool  groupVisibleFor(const SessionParams& st, int mediaId) const;
    SessionParams bakeGroupVisibility(SessionParams p) const;   // child.visible &&= group
    void  commitStructuralChange();                     // sync panel + render + undo
    QSize activeLayerNativeSize() const;   // layer pixel size at 100% scale
    QSize layerNativeSize(const Layer& l) const;   // any layer's 100%-scale size
    void  pushPreviewTransform();          // feed active transform + selection to the preview
    void selectLayerInternal(int layerId, bool makeVisible);
    QString uniqueLayerName(const SessionParams& p, LayerKind kind) const;
    void syncLayersPanel();
    void scheduleRender(bool previewOnly = false, bool qualityOnly = false);
    float zoomQualityScale() const;   // full-pass supersample for the current zoom
    QHash<int, QImage> layerSourcesAt(const SessionImage& img, int frame) const;
    void pushUndoSnapshot();
    QVector<int> addImages(const QStringList& paths);   // load files into the library only; returns new media ids
    void addLayerFromMedia(int mediaId);        // place a library source as a layer
    int  ensureBoard();                         // make sure the single composition exists
    void importSequence(const QStringList& paths);
    void switchToImage(int index);

    // Animation / timeline
    void setPlayhead(int frame);          // scrub: apply interpolated params + render
    void syncTimeline();                  // push current image's anim → timeline widget
    void onTimelineEdited();              // timeline → state (keyframes moved/changed)
    void onPlayToggled(bool playing);
    bool buildPlayCache();                // pre-render all frames for smooth playback
    void autoKeyChanged(const SessionParams& before, const SessionParams& after);
    void exportSequence(const QString& baseName);
    void exportVideoMp4(const QString& baseName);
    void exportSvg(const QString& baseName);
    void updateDisplayedPreview();
    void updatePreviewInteractionState();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    ControlsPanel*    m_left        = nullptr;
    ModePanel*        m_right       = nullptr;
    PreviewWidget*    m_preview     = nullptr;
    FilmstripWidget*  m_filmstrip   = nullptr;
    TimelineWidget*   m_timeline    = nullptr;
    LayersPanel*      m_layersPanel = nullptr;
    RenderWorker*     m_worker      = nullptr;

    QVector<SessionImage> m_images;
    int                   m_current = -1;
    QSet<int>             m_selection;   // canvas multi-selection (runtime, not undone)

    QImage m_lastRender;
    QImage m_lastPreviewFrame;
    bool   m_capsLockActive = false;
    bool   m_spaceDown = false;
    bool   m_transformDragging = false;   // live on-canvas drag → cheap preview only
    QTimer m_undoTimer;
    QTimer m_previewTimer;   // debounce live preview until param edits settle
    QTimer m_zoomRenderTimer; // debounce re-render at higher res after zooming

    QTimer          m_playTimer;
    bool            m_autoKey = false;
    bool            m_playing = false;
    QVector<QImage> m_playCache;             // pre-rendered frames for playback
    bool            m_playCacheValid = false;
};
