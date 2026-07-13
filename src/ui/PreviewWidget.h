#pragma once

#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QSet>
#include <QHash>
#include <QVector>
#include "../core/Params.h"

class QKeyEvent;

/**
 * PreviewWidget
 *
 * Displays the processed image, scaled to fit while preserving aspect
 * ratio, with a checkerboard under transparent areas.
 * Accepts image drops (multiple files at once).
 */
class PreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    void setOriginalImage(const QImage& img);
    void setStatus(const QString& text);
    void resetZoom();
    void setShowOriginal(bool show);
    void setPanMode(bool enabled);

    // Current zoom (1.0 = fit). The UI re-renders at higher resolution when
    // zoomed in, so the vector symbols stay crisp instead of upscaling a raster.
    double zoomFactor() const { return m_zoomFactor; }

    // True while an active move drag is magnet-snapped to a frame edge/centre:
    // the position is momentarily locked in place, so MainWindow can afford a
    // full-quality render right away instead of the usual downscaled drag
    // preview (which would otherwise visibly sharpen a beat after release).
    bool isSnapped() const { return m_snapped; }

    // Active layer placement for the on-canvas transform handles. layerNative is
    // the layer's pixel size at 100% scale; frame is the composited canvas size.
    void setActiveTransform(const LayerTransform& tf, QSize layerNative,
                            QSize frame, bool transformable);

    // One placeable layer's geometry, used for click/box selection and outlines.
    struct CanvasLayer { int id; LayerTransform tf; QSize native; };
    // All visible layers, top-most first (matches the layer stack order).
    void setCanvasLayers(const QVector<CanvasLayer>& layers, QSize frame);
    void setSelection(const QSet<int>& selected, int activeId);

    // Halftone localize-diameter point overlay for the active layer.
    void setHalftoneLoc(const HalftoneLocPoint& pt, QSize frame, bool visible);

signals:
    void filesDropped(const QStringList& paths);
    void mediaDroppedAsLayer(int mediaId);   // a library thumbnail dropped on the canvas
    void transformChanged(const LayerTransform& t);
    void groupTransformChanged(const QHash<int, LayerTransform>& byId);  // multi-select gizmo
    void transformEditFinished();   // a move/scale/rotate drag ended → do a full render
    void selectionChanged(const QSet<int>& ids, int activeId);   // canvas click/box select
    void zoomChanged();   // user zoomed; UI may re-render at the new resolution
    void localizationChanged(const HalftoneLocPoint& pt);   // loc dot dragged
    void localizationEditFinished();                        // drag ended → full render
    void localizationDeleteRequested();                      // Backspace on selected loc dot

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QImage  m_image;
    QImage  m_originalImage;
    QString m_status;

    // Cached scaled image to avoid re-scaling on every paint
    QImage  m_scaled;
    double  m_zoomFactor = 1.0;
    QPoint  m_panOffset;
    QPoint  m_lastDragPos;
    bool    m_showOriginal = false;
    bool    m_panMode = false;
    bool    m_dragging = false;
    Qt::MouseButton m_dragButton = Qt::NoButton;
    void    updateScaled();
    const QImage& currentSource() const;

    // ── On-canvas transform handles for the active layer ──────────
    enum class TfDrag { None, Move, Scale, Rotate };
    LayerTransform m_tf;
    QSize          m_layerNative;
    QSize          m_frame;
    bool           m_transformable = false;
    TfDrag         m_tfDrag        = TfDrag::None;
    LayerTransform m_tfStart;          // transform at drag start
    QPointF        m_grabOffset;       // Move: centre - grab point (frame space)
    double         m_startDist  = 1.0; // Scale: |corner - centre| at start
    double         m_startAngle = 0.0; // Rotate: angle(grab - centre) at start

    // ── Selection (click / box) over all layers ───────────────────
    QVector<CanvasLayer> m_canvasLayers;   // top-first
    QSet<int>            m_selection;
    int                  m_activeId = -1;
    bool                 m_boxSelecting = false;
    bool                 m_boxAdditive  = false;   // shift held → add to selection
    QPoint               m_boxStart;
    QPoint               m_boxCur;
    bool                 m_snapped = false;   // current move drag is edge/centre-locked

    // ── Group gizmo for a multi-layer selection (>= 2 layers) ──────
    bool                       m_groupDrag = false;
    TfDrag                     m_groupMode = TfDrag::None;
    QHash<int, LayerTransform> m_groupStart;     // per-layer transform at drag start
    QPointF                    m_groupPivot;     // bbox centre (frame space), fixed for the drag
    QPointF                    m_groupGrabFrame; // Move: grab point in frame space

    double  imageScale() const;        // frame px → widget px factor (0 if none)
    QPointF imageOrigin() const;       // top-left of m_scaled in widget coords
    QPointF frameToWidget(QPointF f) const;
    QPointF widgetToFrame(QPointF w) const;
    QPointF layerCentreFrame() const;
    QPolygonF layerQuadFrame() const;  // active layer: 4 corners in frame space
    QPolygonF quadFrame(const LayerTransform& tf, QSize native) const;  // any layer
    QPolygonF quadWidget(const LayerTransform& tf, QSize native) const;
    bool      handlesVisible() const;  // single selected, transformable, editable
    int       hitTest(QPointF widgetPos) const;   // top-most layer id under point, -1
    const CanvasLayer* layerById(int id) const;
    void    paintHandles(QPainter& p);

    // Group gizmo helpers (multi-selection).
    bool      groupHandlesVisible() const;        // >= 2 selected, editable
    QRectF    groupBBoxFrame() const;             // AABB of selected layers, frame space
    QRectF    groupRectWidget() const;            // same box mapped to widget space
    QPointF   centreFrame(const LayerTransform& tf) const;  // layer centre in frame space
    void      paintGroupHandles(QPainter& p);

    // Magnetic snap to the frame's edges/centre while moving (single layer or
    // group): nudges the drag delta so a bounding box within a screen-constant
    // threshold locks onto the nearest frame edge or midline.
    QPointF   snapDeltaForBBox(const QRectF& bboxFrame) const;
    QRectF    layerBBoxAt(const QPointF& centreFrame, const LayerTransform& tf, QSize native) const;

    // ── Halftone localize-diameter dot overlay ─────────────────────
    enum class LocDrag { None, Move, Radius, Falloff };
    HalftoneLocPoint m_loc;
    QSize            m_locFrame;
    bool             m_locVisible  = false;
    bool             m_locSelected = false;   // can be deleted with Backspace
    bool             m_locHovered  = false;   // passive-mode hover → dot grows
    LocDrag          m_locDrag     = LocDrag::None;
    QPointF          m_locGrabOffset;         // Move: centre - grab point (frame space)

    QPointF locCentreFrame() const;
    double  locRadiusFramePx() const;   // outer ring, frame px
    double  locInnerFramePx() const;    // falloff ring, frame px
    void    paintLocHandles(QPainter& p);
};
