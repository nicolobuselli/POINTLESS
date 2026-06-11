#pragma once

#include <QWidget>
#include <QImage>
#include <QList>

class QScrollArea;
class QHBoxLayout;
class FilmstripThumb;
class ChevronButton;

/**
 * FilmstripWidget (bottom bar)
 *
 * Orange "add" button + horizontally scrollable thumbnails of every
 * image loaded in the session. Click a thumbnail to switch, hover ✕
 * to close it. Accepts image drops.
 */
class FilmstripWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FilmstripWidget(QWidget* parent = nullptr);

    int  addThumb(const QImage& source, const QString& name);
    void removeThumb(int index);
    void setActive(int index);
    int  count() const { return m_thumbs.size(); }

signals:
    void addRequested();
    void filesDropped(const QStringList& paths);
    void thumbSelected(int index);
    void thumbCloseRequested(int index);

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
