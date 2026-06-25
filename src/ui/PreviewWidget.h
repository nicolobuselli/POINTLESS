#pragma once

#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QSet>
#include <QVector>
#include "../core/Params.h"

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

    // Active layer placement for the on-canvas transform handles. layerNative is
    // the layer's pixel size at 100% scale; frame is the composited canvas size.
    void setActiveTransform(const LayerTransform& tf, QSize layerNative,
                            QSize frame, bool transformable);

    // One placeable layer's geometry, used for click/box selection and outlines.
    struct CanvasLayer { int id; LayerTransform tf; QSize native; };
    // All visible layers, top-most first (matches the layer stack order).
    void setCanvasLayers(const QVector<CanvasLayer>& layers, QSize frame);
    void setSelection(const QSet<int>& selected, int activeId);

signals:
    void filesDropped(const QStringList& paths);
    void mediaDroppedAsLayer(int mediaId);   // a library thumbnail dropped on the canvas
    void transformChanged(const LayerTransform& t);
    void transformEditFinished();   // a move/scale/rotate drag ended → do a full render
    void selectionChanged(const QSet<int>& ids, int activeId);   // canvas click/box select
    void zoomChanged();   // user zoomed; UI may re-render at the new resolution

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

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
};
