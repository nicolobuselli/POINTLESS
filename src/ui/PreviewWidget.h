#pragma once

#include <QWidget>
#include <QImage>

/**
 * PreviewWidget
 *
 * Custom widget that displays the halftone-processed image,
 * scaled to fit while preserving aspect ratio.
 * Shows a "drop file here" placeholder when no image is loaded.
 */
class PreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    void setStatus(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

signals:
    void fileDropped(const QString& path);

private:
    QImage  m_image;
    QString m_status;

    // Cached scaled image to avoid re-scaling on every paint
    QImage  m_scaled;
    void    updateScaled();
};
