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
 *
 * The pages edit the active layer's settings. Clicking a tab emits
 * modeSelected() so MainWindow can select (or create) a layer of
 * that kind. When the Original layer is active the pages are
 * disabled — only the left adjustments apply to it.
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

    QString outputFileName() const;
    QString outputFormat()   const;

    void setFromLayer(const Layer& layer);   // silent

signals:
    void paramsChanged();
    void modeSelected(RenderMode m);   // user clicked a tab
    void exportRequested();

private:
    void setMode(RenderMode m);   // silent: updates tabs + visible page

    QPushButton* m_tabHalftone = nullptr;
    QPushButton* m_tabDither   = nullptr;
    QPushButton* m_tabAscii    = nullptr;

    HalftonePage* m_halftonePage = nullptr;
    DitherPage*   m_ditherPage   = nullptr;
    AsciiPage*    m_asciiPage    = nullptr;

    QLineEdit*       m_outputName = nullptr;
    NoWheelComboBox* m_format     = nullptr;

    RenderMode m_mode     = RenderMode::Halftone;
    bool       m_updating = false;
};
