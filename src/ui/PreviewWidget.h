#pragma once

#include <QWidget>
#include <QImage>
#include <QPoint>

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

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

signals:
    void filesDropped(const QStringList& paths);

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
};
