#pragma once

#include <QWidget>
#include <QImage>
#include <QList>
#include <vector>
#include "../core/Params.h"

class QVBoxLayout;
class QPushButton;
class NoWheelComboBox;
class ChevronButton;
class LayerRow;
class RowsArea;
class TrashButton;

/**
 * LayersPanel
 *
 * Floating panel anchored to the top-right corner of the preview.
 * Shows the layer stack of the current image (top layer first):
 * per-layer reference thumbnail (source + that layer's adjustments),
 * name and an eye toggle per row, plus a Photoshop-style blend-mode
 * combo and a trash button for the selected layer.
 *
 * Rows can be dragged to reorder the stack or dropped on the trash
 * to delete. "+" duplicates the selected layer (or adds a layer of
 * the current mode). Collapses to a small icon button via the
 * right-pointing chevron.
 */
class LayersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayersPanel(QWidget* parent = nullptr);

    void setLayers(const std::vector<Layer>& layers, int activeId);
    void setSourceImage(const QImage& source);
    void setBackground(const QColor& background, float opacity);

signals:
    void visibilityToggled(int layerId, bool visible);
    void layerSelected(int layerId);
    void layerRenamed(int layerId, const QString& name);
    void deleteRequested(int layerId);
    void blendModeChanged(int layerId, BlendMode mode);
    void addLayerRequested();
    void reorderRequested(int layerId, int insertIndex);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

private:
    void setExpandedUi(bool expanded);
    void rebuildRows();
    void updateRowsInPlace();
    void syncFooter();
    void reposition();
    QPixmap thumbFor(const Layer& layer) const;

    std::vector<Layer> m_layers;
    int                m_activeId = -1;
    QImage             m_smallSource;   // downscaled source for row thumbs
    QColor             m_background    = QColor(0x0A,0x0A,0x0A);
    float              m_bgOpacity     = 1.0f;

    QWidget*         m_expandedBox  = nullptr;
    QPushButton*     m_collapsedBtn = nullptr;
    RowsArea*        m_rowsArea     = nullptr;
    QList<LayerRow*> m_rows;
    NoWheelComboBox* m_blendCombo   = nullptr;
    TrashButton*     m_trashBtn     = nullptr;
    bool             m_isExpanded   = true;
    bool             m_updating     = false;
};
