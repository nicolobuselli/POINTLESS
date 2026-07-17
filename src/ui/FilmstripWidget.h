#pragma once

#include <QWidget>
#include <QImage>
#include <QList>

class QScrollArea;
class QGridLayout;
class QResizeEvent;
class QLabel;
class FilmstripThumb;
class AddImageButton;

/**
 * FilmstripWidget (bottom bar) — the source LIBRARY.
 *
 * Orange "add" button (sized to match a thumbnail cell, always visible above
 * the grid — never scrolls) + a square grid of thumbnails, one per library
 * source (identified by a media id): images cover-fill their cell regardless
 * of source aspect ratio, wrap to a new row once the column count is full,
 * and scroll vertically once rows overflow the visible height. The column
 * count grows on wide windows instead of letting cells balloon past
 * kMaxCellFigmaPx. Single click selects, double click adds it as a layer,
 * dragging a thumbnail onto the Layers panel / canvas adds it as a layer,
 * hover ✕ removes it from the library. Accepts image file drops.
 */
class FilmstripWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FilmstripWidget(QWidget* parent = nullptr);

    void addThumb(int mediaId, const QImage& source, const QString& name);
    void removeThumb(int mediaId);
    void clear();   // drops every thumb (e.g. before loading a different project)
    void setActive(int mediaId);
    int  count() const { return m_thumbs.size(); }

signals:
    void addRequested();
    void filesDropped(const QStringList& paths);
    void thumbSelected(int mediaId);    // single click
    void thumbActivated(int mediaId);   // double click → add as layer
    void thumbCloseRequested(int mediaId);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    static constexpr int kMinColumns      = 6;     // floor, even on a narrow window
    static constexpr int kMaxCellFigmaPx  = 150;    // cap on a cell's side; wide windows add columns instead

    int  hMargins() const;         // hl's own left+right content margins
    int  computeColumns() const;   // column count for the current viewport width
    void relayoutGrid();           // re-seats every thumb at its row/col + resizes them
    void applyCellSizes();         // resize only; falls back to relayoutGrid if the column count changed
    void applyCellSizesOnly();     // resize every thumb + the add button to the current column count

    QScrollArea*           m_scroll      = nullptr;
    QWidget*               m_thumbRow    = nullptr;
    QGridLayout*           m_thumbLayout = nullptr;
    AddImageButton*        m_addBtn      = nullptr;
    QLabel*                m_emptyHint   = nullptr;
    QList<FilmstripThumb*> m_thumbs;
    int                    m_columns     = kMinColumns;
};
