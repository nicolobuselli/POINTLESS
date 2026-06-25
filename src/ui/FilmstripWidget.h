#pragma once

#include <QWidget>
#include <QImage>
#include <QList>

class QScrollArea;
class QHBoxLayout;
class FilmstripThumb;
class ChevronButton;

/**
 * FilmstripWidget (bottom bar) — the source LIBRARY.
 *
 * Orange "add" button + horizontally scrollable thumbnails, one per library
 * source (identified by a media id). Single click selects, double click adds
 * it as a layer, dragging a thumbnail onto the Layers panel / canvas adds it
 * as a layer, hover ✕ removes it from the library. Accepts image file drops.
 */
class FilmstripWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FilmstripWidget(QWidget* parent = nullptr);

    void addThumb(int mediaId, const QImage& source, const QString& name);
    void removeThumb(int mediaId);
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

private:
    void scrollBy(int dx);

    QScrollArea*           m_scroll      = nullptr;
    QWidget*               m_thumbRow    = nullptr;
    QHBoxLayout*           m_thumbLayout = nullptr;
    ChevronButton*         m_leftBtn     = nullptr;
    ChevronButton*         m_rightBtn    = nullptr;
    QList<FilmstripThumb*> m_thumbs;
};
