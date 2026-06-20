#pragma once

#include "../core/Animation.h"
#include <QWidget>
#include <functional>

class QSpinBox;
class QPushButton;
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

private:
    friend class TimelineCanvas;
    void syncControls();      // m_anim → spinboxes (silent)
    void emitEdited();        // notify owner of an animation edit
    void scrubTo(int frame);  // playhead change from canvas/controls
    void jumpKey(int dir);    // -1 prev / +1 next keyframe

    Animation m_anim;
    bool      m_updating = false;

    QPushButton* m_autoKeyBtn = nullptr;
    QPushButton* m_playBtn    = nullptr;
    QSpinBox*    m_frameSpin  = nullptr;   // current frame (scrubs)
    QSpinBox*    m_startSpin  = nullptr;
    QSpinBox*    m_endSpin    = nullptr;
    QSpinBox*    m_fpsSpin    = nullptr;   // kept (hidden) for export fps
    TimelineCanvas* m_canvas  = nullptr;
};
