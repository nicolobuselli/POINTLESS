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
#include <QCloseEvent>
#include "../core/Params.h"
#include "../core/Animation.h"
#include "../gpu/GpuFramePackage.h"

class PreviewWidget;
class ControlsPanel;
class ModePanel;
class FilmstripWidget;
class TimelineWidget;
class LayersPanel;
class RenderWorker;
class WinTitleBar;

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

    // Loads a .less file, replacing the current composition — used for the
    // Ctrl+O dialog and for a path handed in on the command line (double-
    // clicking a .less file once file association is registered).
    void openProjectFromPath(const QString& path);

private slots:
    void onParamsChanged();
    void onRenderComplete(QImage result, bool isPreview);
    void onLayersComplete(GpuFramePackage pkg, bool isPreview);
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
    void onParentSelected(int mediaId);
    void onLayerRangeRequested(int layerId);
    void onLayerToggleRequested(int layerId);
    void onLayerRenamed(int layerId, const QString& name);
    void onLayerDeleteRequested(int layerId);
    void onLayerRemoveEditsRequested(int layerId);
    void onLayerBlendChanged(int layerId, BlendMode mode);
    void onLayerNoModeOpacityChanged(float opacity);
    void onLayerTransformChanged(const LayerTransform& t);
    void onGroupTransformChanged(const QHash<int, LayerTransform>& byId);
    void onLocalizationChanged(LocParam p, const LocPoint& pt);
    void onLocalizationToggleRequested(LocParam p);
    void onLocalizeToggleRequested();   // Adjustments' single mask button
    void onCanvasSelectionChanged(const QSet<int>& ids, int activeId);
    void onAddLayerRequested();
    void onLayerReordered(int layerId, int insertIndex);
    void onLayerDuplicateRequested(int layerId, int insertIndex);   // Alt+drag
    void onCopyLayerRequested(int layerId);
    void onPasteLayerRequested(int layerId);
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
    void  pushPreviewTransform();          // feed active transform + selection + loc dot to the preview
    void selectLayerInternal(int layerId, bool makeVisible);
    QString uniqueLayerName(const SessionParams& p, LayerKind kind, int mediaId) const;
    QString uniqueDuplicateName(const SessionParams& p, const QString& baseName) const;
    void syncLayersPanel();
    void pasteLayerBelowActive();   // Ctrl+V on a focused layer row: paste right under it
    void scheduleRender(bool previewOnly = false, bool qualityOnly = false);
    QHash<int, QImage> layerSourcesAt(const SessionImage& img, int frame) const;
    void pushUndoSnapshot();
    QVector<int> addImages(const QStringList& paths);   // load files into the library only; returns new media ids
    int  addImageToLibrary(const QImage& img, const QString& name);   // in-memory image (e.g. clipboard paste) → library
    void addLayerFromMedia(int mediaId);        // place a library source as a layer
    int  ensureBoard();                         // make sure the single composition exists
    void importSequence(const QStringList& paths);
    void switchToImage(int index);
    bool saveProject(bool forceDialog);   // Ctrl+S; forceDialog=true always shows Save As. false = cancelled/failed
    void openProject();                   // Ctrl+O; replaces the current composition
    bool isDirty() const;                 // current state differs from the last save/load/empty baseline

    // Animation / timeline
    void setPlayhead(int frame);          // scrub: apply interpolated params + render
    void syncTimeline();                  // push current image's anim → timeline widget
    void onTimelineEdited();              // timeline → state (keyframes moved/changed)
    void onPlayToggled(bool playing);
    // pre-render all frames for smooth playback. `dialogDelayMs` gates how long
    // a rebuild must take before the progress dialog shows — the initial Play
    // press wants quick feedback (short delay), a silent rebake mid-playback
    // (state edited while already looping) should stay invisible unless it's
    // genuinely slow, or it flashes on every edit/scrub.
    bool buildPlayCache(int dialogDelayMs = 300);
    // True when every visible layer's mode is GPU-renderable (see each
    // *Renderer::gpuRenderable) and the GPU compositor is active — playback
    // can then render each frame live instead of pre-baking the whole range.
    bool animCanPlayLive() const;
    void autoKeyChanged(const SessionParams& before, const SessionParams& after);
    void autoKeyTransform(int layerId, const LayerTransform& before, const LayerTransform& after);
    void autoKeyLocalization(int layerId, LocParam p, const LocPoint& before, const LocPoint& after);
    void refreshAnimationIndicators();   // push "has a keyframe track" state to the panels
    bool insertKeyframeUnderCursor();    // "I" key: keyframe whatever control is under the mouse
    void exportSequence(const QString& baseName);
    void exportVideoMp4(const QString& baseName);
    void exportSvg(const QString& baseName);
    void updateDisplayedPreview();
    void updatePreviewInteractionState();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;   // prompts to save unsaved changes
#ifdef Q_OS_WIN
    // Custom client-drawn title bar: strip the native caption via WM_NCCALCSIZE
    // while keeping native resize/snap/shadow (WM_NCHITTEST borders + caption).
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    ControlsPanel*    m_left        = nullptr;
    ModePanel*        m_right       = nullptr;
    PreviewWidget*    m_preview     = nullptr;
    FilmstripWidget*  m_filmstrip   = nullptr;
    TimelineWidget*   m_timeline    = nullptr;
    LayersPanel*      m_layersPanel = nullptr;
    RenderWorker*     m_worker      = nullptr;
    WinTitleBar*      m_titleBar    = nullptr;

    QVector<SessionImage> m_images;
    int                   m_current = -1;
    QSet<int>             m_selection;   // canvas multi-selection (runtime, not undone)
    int                   m_selAnchor = -1;   // anchor layer for shift-range selection
    Layer                 m_layerClipboard;
    bool                  m_hasLayerClipboard = false;
    int                   m_selectedParentMediaId = -1;   // parent row selected (Backspace deletes group)

    QImage m_lastRender;
    QImage m_lastPreviewFrame;
    GpuFramePackage m_lastPkgRender;    // GPU-composited counterparts of the two above
    GpuFramePackage m_lastPkgPreview;
    bool   m_gpuMode = false;           // GPU compositor confirmed working
    bool   m_capsLockActive = false;
    bool   m_spaceDown = false;
    bool   m_transformDragging = false;   // live on-canvas drag → cheap preview only
    bool   m_locDragging = false;         // live loc-dot drag → cheap preview only
    QTimer m_undoTimer;
    QTimer m_previewTimer;   // debounce live preview until param edits settle

    QString       m_projectPath;    // last save/open .less path; empty → Ctrl+S prompts Save As
    SessionParams m_savedParams;    // state as of the last save/load/empty-board — isDirty() diffs against this
    Animation     m_savedAnim;

    QTimer          m_playTimer;
    bool            m_autoKey = false;
    bool            m_playing = false;
    bool            m_playLive = false;      // true: render each tick live, no pre-bake
    QVector<QImage> m_playCache;             // pre-rendered frames for playback
    bool            m_playCacheValid = false;
    SessionParams   m_playCacheParams;       // state/anim the cache was baked from —
    Animation       m_playCacheAnim;         // any mismatch means the cache is stale

    // Bottom Timeline/Library panel: collapses to just its tab titles when
    // dragged down past a threshold (see MainWindow ctor).
    bool m_bottomCollapsed  = false;
    int  m_bottomExpandedH  = 150;   // default height restored when a title is clicked from collapsed
    int  m_bottomLastPage   = 0;     // 0=Library, 1=Timeline — restored when re-expanding via drag
};
