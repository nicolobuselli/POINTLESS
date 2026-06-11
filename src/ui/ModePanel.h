#pragma once

#include "../core/Params.h"
#include "Widgets.h"
#include <QWidget>

class HalftonePage;
class DitherPage;
class AsciiPage;

/**
 * ModePanel (right column)
 *
 * Tab row (Halftone / Dither / Ascii) on top, the active page's
 * settings in a scroll area, and the shared Background color
 * pinned at the bottom.
 */
class ModePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ModePanel(QWidget* parent = nullptr);

    RenderMode       mode() const { return m_mode; }
    HalftoneSettings halftoneSettings() const;
    DitherSettings   ditherSettings()   const;
    AsciiSettings    asciiSettings()    const;
    QColor           background()        const;
    float            backgroundOpacity() const;

    void setAll(const SessionParams& p);   // silent

signals:
    void paramsChanged();

private:
    void setMode(RenderMode m, bool notify);

    QPushButton* m_tabHalftone = nullptr;
    QPushButton* m_tabDither   = nullptr;
    QPushButton* m_tabAscii    = nullptr;

    HalftonePage* m_halftonePage = nullptr;
    DitherPage*   m_ditherPage   = nullptr;
    AsciiPage*    m_asciiPage    = nullptr;

    FillSwatch* m_bgSwatch = nullptr;

    RenderMode m_mode     = RenderMode::Halftone;
    bool       m_updating = false;
};
