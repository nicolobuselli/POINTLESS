#pragma once

#include "../core/Animation.h"
#include <QWidget>
#include <QVector>
#include <QHash>
#include <functional>

class QSpinBox;
class QPushButton;
class QScrollBar;
class QSlider;
class PopupPicker;
class TimelineCanvas;

/**
 * TimelineWidget — dopesheet-style keyframe timeline (bottom bar).
 *
 * Shows one row per animated track with draggable keyframe diamonds, a
 * scrubbable playhead, and a control bar (Auto-key, playback, frame range,
 * fps). It edits a local copy of the Animation and reports changes through
 * std::function callbacks, mirroring the app's other panels.
 */
class TimelineWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    // One row per video clip in the board, above the keyframe tracks: name in
    // the label gutter, a bar spanning the clip's duration with draggable
    // in/out edges (trimmed-away part stays visible, dark).
    struct ClipRow {
        int     mediaId = -1;
        QString name;
        int     length  = 0;   // source frames
        int     trimIn  = 0;   // clip-local, inclusive
        int     trimOut = 0;   // clip-local, inclusive
        int     offset  = 0;   // clip-local frame 0 → this many frames past frameStart
    };
    // `layerMedia` maps layerId → mediaId, so a keyframe track can be shown
    // nested under the clip row of the video it animates. `layerNames` gives
    // still-image layers the same parent/child grouping: a header row with the
    // layer's name, its parameter tracks indented under it.
    void setClips(const QVector<ClipRow>& clips, const QHash<int, int>& layerMedia = {},
                  const QHash<int, QString>& layerNames = {});

    // Last frame the timeline scrolls to: past the animation range *and* past
    // the last clip, plus a fixed tail of working space.
    int dispEndFrame() const;

    void      setAnimation(const Animation& a);   // silent: refresh UI
    Animation animation() const { return m_anim; }
    void      setPlayheadSilent(int frame);       // playback/scrub display only
    bool      autoKey() const;
    void      togglePlay();                        // Space shortcut from MainWindow
    void      setPlayingSilent(bool on);           // sync play button without emitting
    void      copyKeys();                           // Ctrl+C: copy selected keyframes
    void      pasteKeys();                          // Ctrl+V: paste at the playhead
    bool      deleteSelectedKeys();                 // Backspace: delete selected keys (true if any)

    // Callbacks (set by MainWindow)
    std::function<void(int)>  onPlayheadChanged;  // scrub / frame jump
    std::function<void()>     onAnimEdited;        // keyframes / range / fps changed
    std::function<void(bool)> onPlayToggled;       // play/pause
    std::function<void(bool)> onAutoKeyToggled;
    std::function<void()>     onImportSequence;
    std::function<void(int, int, int, int)> onClipEdited;   // mediaId, offset, trimIn, trimOut

private:
    friend class TimelineCanvas;
    // One painted row: a clip bar (clip >= 0), a keyframe track (track >= 0),
    // or a plain layer header (both -1, `name` filled) for still images, which
    // have no clip bar to hang under. A track row that belongs to the clip or
    // header above it is drawn indented, with an L connector.
    struct Row { int clip = -1; int track = -1; bool child = false; QString name; };
    void rebuildRows();
    void updateCanvasHeight();
    void updateScrollRange();          // canvas width/zoom/range → h-scrollbar
    void setZoom(double z, int anchorFrame, int anchorX);   // keep anchorFrame under anchorX
    void setScroll(double firstFrame);
    void syncControls();      // m_anim → spinboxes (silent)
    void emitEdited();        // notify owner of an animation edit
    void scrubTo(int frame);  // playhead change from canvas/controls
    void jumpKey(int dir);    // -1 prev / +1 next keyframe

    Animation        m_anim;
    QVector<ClipRow> m_clips;
    QHash<int, int>     m_layerMedia;   // layerId → mediaId
    QHash<int, QString> m_layerNames;   // layerId → name (header rows for stills)
    QVector<Row>     m_rows;         // painted row order (clips + their tracks)
    bool             m_updating = false;
    double           m_zoom     = 1.0;   // 1 = whole working span fits the view
    double           m_scroll   = 0.0;   // first visible frame, relative to frameStart

    QPushButton* m_autoKeyBtn = nullptr;
    QPushButton* m_playBtn    = nullptr;
    QSpinBox*    m_frameSpin  = nullptr;   // current frame (scrubs)
    QSpinBox*    m_startSpin  = nullptr;
    QSpinBox*    m_endSpin    = nullptr;
    PopupPicker* m_fpsPicker  = nullptr;   // Native / 24 / 15 / 12 / 8 fps
    QScrollBar*  m_hbar       = nullptr;   // horizontal pan over the frame span
    QSlider*     m_zoomSlider = nullptr;
    TimelineCanvas* m_canvas  = nullptr;
};
