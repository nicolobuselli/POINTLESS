#pragma once

#include "../core/Params.h"
#include "Widgets.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QImage>
#include <QString>
#include <functional>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class SwatchStrip;
class ChevronButton;
class PalettePopup;
class SavePalettePopup;

/**
 * TonalControlsWidget
 *
 * Color strategy for a layer. A custom Palette selector sits on top: its
 * header shows the current palette name and a preview of the colors in
 * use; clicking it opens a floating dropdown (over the controls below)
 * listing the saved palette library, each row with a color preview and a
 * trash button — deleting asks for confirmation inline.
 *
 * Below it, one row holds the color-count dropdown and a "Generate random"
 * button (the count is chosen once and drives both the tones and the
 * generator):
 *   "Image colors"       → sample colors from the picture
 *   "1 color".."5 color" → map luminosity onto N tones
 * Each tone row = color swatch + level slider. The user can save the
 * current colors as a named palette. Editing anything flips the selector
 * back to "Custom".
 */
class TonalControlsWidget : public QWidget
{
public:
    std::function<void()> onChanged;

    explicit TonalControlsWidget(const TonalSettings& initial,
                                 QWidget* parent = nullptr);

    TonalSettings settings() const { return m_settings; }
    void setSettings(const TonalSettings& s);   // silent

    // Source image for the "Extract from image" palette generator.
    void setSourceImage(const QImage& img) { m_sourceImage = img; }

private:
    // Tone rows / mode
    void rebuildRows();
    void syncModeCombo();
    void emitChanged();
    void openTonePicker(int idx, QWidget* anchor);
    static QStringList labelsFor(int n);

    // Palette selector
    void reloadLibrary();
    void openPalettePopup();
    void applyPalette(const std::vector<QColor>& colors, const QString& name,
                      ToneMode mode = ToneMode::FixedTones);
    void markCustom();                       // flip header label to "Custom"
    void refreshPreview();                   // header name + color strip
    QString matchLibraryName() const;        // library name for current colors, else "Custom"
    std::vector<QColor> currentColors() const;

    // Save / random / extract
    void beginSavePalette();
    void commitSavePalette();
    void cancelSavePalette();
    void generateRandom();
    void extractFromImage();

    TonalSettings m_settings;

    // Palette selector (header + floating popup)
    QWidget*       m_paletteSection = nullptr;
    QLabel*        m_paletteLabel   = nullptr;   // "Palette" caption
    QPushButton*   m_paletteHeader  = nullptr;
    QPushButton*   m_favBtn         = nullptr;   // save-palette (favourite)
    QLabel*        m_paletteName    = nullptr;
    SwatchStrip*   m_palettePreview = nullptr;
    ChevronButton* m_paletteChevron = nullptr;
    PalettePopup*  m_palettePopup   = nullptr;
    qint64         m_lastPopupClose = 0;

    // Color count + random generator (shared row)
    NoWheelComboBox* m_modeCombo   = nullptr;
    QPushButton*     m_generateBtn = nullptr;

    // Extract-from-image (full-width row); palette dithering is enabled
    // automatically when extracting.
    QWidget*     m_extraRow   = nullptr;
    QPushButton* m_extractBtn = nullptr;
    QImage       m_sourceImage;

    // Tone rows
    QWidget*     m_rowsContainer = nullptr;
    QVBoxLayout* m_rowsLayout    = nullptr;

    // Save palette
    QWidget*     m_saveSection  = nullptr;
    QPushButton* m_saveBtn      = nullptr;
    QWidget*     m_saveEditRow  = nullptr;
    QLineEdit*   m_saveNameEdit = nullptr;
    SavePalettePopup* m_savePopup = nullptr;

    std::vector<PalettePreset> m_library;

    struct Row {
        FillSwatch*    swatch = nullptr;
        NoWheelSlider* slider = nullptr;       // level slider
    };
    std::vector<Row> m_rows;

    bool m_updating = false;
};
