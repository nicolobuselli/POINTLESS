#pragma once

#include <QWidget>
#include <QImage>
#include <QHash>
#include <QList>
#include <QSet>
#include <vector>
#include "../core/Params.h"

class QVBoxLayout;
class QPushButton;
class NoWheelComboBox;
class ChevronButton;
class LayerRow;
class ParentRow;
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
    // embedded=true → renders only the scrollable row list (no floating
    // chrome, no collapse button, no blend/trash footer), for hosting inside
    // the left column. embedded=false keeps the legacy floating panel.
    explicit LayersPanel(bool embedded = false, QWidget* parent = nullptr);

    // Cascade tree: parent groups (source images, not in the frame) each with
    // their child layers (mediaId == parent's). mediaImages = per-media source
    // for parent thumbnails.
    void setTree(const std::vector<ParentGroup>& parents,
                 const std::vector<Layer>& layers, int activeId,
                 const QHash<int, QImage>& mediaImages);
    void setLayers(const std::vector<Layer>& layers, int activeId);   // legacy flat
    void setSelection(const QSet<int>& sel);   // multi-select highlight (shift-range)
    void setSourceImage(const QImage& source);
    void setBackground(const QColor& background, float opacity);
    void requestAddLayer();   // external "+" trigger (embedded header lives in ControlsPanel)

signals:
    // Child (layer) signals
    void visibilityToggled(int layerId, bool visible);
    void layerSelected(int layerId);
    void layerRangeRequested(int layerId);   // shift-click: select anchor→here
    void layerToggleRequested(int layerId);  // ctrl-click: toggle in/out of selection
    void layerRenamed(int layerId, const QString& name);
    void deleteRequested(int layerId);
    void removeEditsRequested(int layerId);
    void blendModeChanged(int layerId, BlendMode mode);
    void addLayerRequested();
    void reorderRequested(int layerId, int insertIndex);
    void duplicateChildRequested(int layerId, int insertIndex);   // Alt+drag
    void copyLayerRequested(int layerId);    // context menu "Copy layer"
    void pasteLayerRequested(int layerId);   // context menu "Paste layer"
    // Parent (group) signals
    void addChildRequested(int mediaId);
    void parentReordered(int mediaId, int insertIndex);
    void groupVisibilityToggled(int mediaId, bool visible);
    void collapseToggled(int mediaId, bool collapsed);
    void duplicateParentRequested(int mediaId);
    void deleteParentRequested(int mediaId);
    void parentRenamed(int mediaId, const QString& name);
    void mediaDroppedAsLayer(int mediaId);   // library source dropped onto the panel

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

private:
    void setExpandedUi(bool expanded);
    void refreshRows();          // in-place if structure matches, else rebuild
    void buildTree();            // embedded: parent groups + nested children
    void updateRowsInPlace();
    void syncFooter();
    void reposition();
    QPixmap thumbFor(const Layer& layer) const;
    QPixmap parentThumb(int mediaId) const;

    bool                     m_embedded = false;
    std::vector<Layer>       m_layers;
    std::vector<ParentGroup> m_parents;
    QHash<int, QImage>       m_mediaImages;   // mediaId → small source for thumbs
    int                m_activeId = -1;
    QSet<int>          m_selSet;          // extra highlighted rows (range selection)
    QImage             m_smallSource;   // downscaled source for row thumbs (legacy)
    QColor             m_background    = QColor(0x0A,0x0A,0x0A);
    float              m_bgOpacity     = 1.0f;

    QWidget*         m_expandedBox  = nullptr;
    QPushButton*     m_collapsedBtn = nullptr;
    QWidget*         m_rowsScroll   = nullptr;   // embedded: scroll host for rows
    RowsArea*         m_rowsArea     = nullptr;
    QList<LayerRow*>  m_rows;
    QList<ParentRow*> m_parentRows;
    QString           m_treeSig;     // structure signature → rebuild vs in-place
    NoWheelComboBox* m_blendCombo   = nullptr;
    TrashButton*     m_trashBtn     = nullptr;
    bool             m_isExpanded   = true;
    bool             m_updating     = false;
    QPoint           m_dragStart;
    QPoint           m_customPosition;
    bool             m_hasCustomPosition = false;
    QWidget*         m_headerWidget = nullptr;
};
