#pragma once

#include "../core/Params.h"
#include <QWidget>
#include <QImage>
#include <QColor>

class AdjustmentsPanel;
class LayersPanel;
class QLineEdit;

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

signals:
    void adjustmentsChanged();
    void resetRequested();
    void fileRenamed(const QString& name);

private:
    LayersPanel*      m_layers    = nullptr;
    QLineEdit*        m_fileTitle = nullptr;
    AdjustmentsPanel* m_adjust    = nullptr;

    bool m_updating = false;
};
