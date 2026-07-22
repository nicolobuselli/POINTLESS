#pragma once

#include "../core/Params.h"
#include "../core/AnimParams.h"
#include <QWidget>
#include <QImage>
#include <QColor>
#include <QSet>
#include <QHash>
#include <QLabel>

class AdjustmentsPanel;
class LayersPanel;
class DragSpinBox;
class QLineEdit;
class QSlider;
class QPushButton;

/**
 * ControlsPanel (left column)
 *
 * Filename (editable) · embedded Layers list · image-adjustment Parameters.
 * Palette (Fill) and Background now live in the right ModePanel.
 */
class ControlsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ControlsPanel(QWidget* parent = nullptr);

    // Embedded Layers list (top of the column). MainWindow wires its signals.
    LayersPanel* layers() const { return m_layers; }

    // Editable file name (top of the column).
    void setFileName(const QString& name);

    // Parameters (image adjustments)
    Adjustments adjustments() const;
    void        setAdjustments(const Adjustments& a);   // silent
    void        setSourceImage(const QImage& img);
    void        setLocalizeChecked(bool on);            // silent

    // Hide the "+" (add layer) when there's no image to add a layer to.
    void setAddLayerVisible(bool on);

    // Frame dimensions (the canvas all layers are composited onto).
    void setFrameSize(int w, int h);                    // silent

    // Active layer placement on the frame (X/Y in px from centre, scale %, rotation °).
    void setTransform(const LayerTransform& t);         // silent

    // Tint Parameters/Transform labels whose ParamId has a keyframe track.
    void setAnimatedParams(const QSet<ParamId>& ids);

    // Control widget → ParamId (Parameters + Transform), for hover-to-keyframe.
    QHash<QWidget*, ParamId> paramWidgets() const;

    // Forwards to AdjustmentsPanel::scrollToTop() — see its doc comment.
    void scrollToTop();

signals:
    void adjustmentsChanged();
    void resetRequested();
    void localizeToggleRequested();
    void fileRenamed(const QString& name);
    void frameSizeChanged(int w, int h);
    void transformChanged(const LayerTransform& t);

private:
    void  emitTransform();
    void  setScale(float scalePct);     // silent — moves slider + box
    float currentScalePct() const;      // reads the box (falls back to slider)

    LayersPanel*      m_layers      = nullptr;
    QPushButton*      m_addLayerBtn = nullptr;
    QLineEdit*        m_fileTitle   = nullptr;
    AdjustmentsPanel* m_adjust    = nullptr;
    DragSpinBox*      m_frameW    = nullptr;
    DragSpinBox*      m_frameH    = nullptr;
    DragSpinBox*      m_tfX       = nullptr;
    DragSpinBox*      m_tfY       = nullptr;
    DragSpinBox*      m_tfRot     = nullptr;
    QSlider*          m_tfScaleSlider = nullptr;
    QLineEdit*        m_tfScaleEdit   = nullptr;
    QPushButton*      m_flipH     = nullptr;   // mirror about y axis (left/right)
    QPushButton*      m_flipV     = nullptr;   // mirror about x axis (top/bottom)
    int               m_curFrameW = 1080;
    int               m_curFrameH = 1080;

    bool m_updating = false;
};
