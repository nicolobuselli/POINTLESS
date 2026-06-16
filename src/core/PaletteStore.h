#pragma once

#include "Params.h"
#include <QColor>
#include <QString>
#include <vector>

class QImage;

// ============================================================
//  PaletteStore
//
//  Persistent palette library backed by QSettings. Seeded from the
//  built-in palettePresets() on first run, then fully user-managed:
//  the user can save new palettes and delete any entry. Colors are
//  serialised as comma-separated #RRGGBB strings.
// ============================================================

namespace PaletteStore {

// All stored palettes in display order (built-ins + user-saved).
std::vector<PalettePreset> all();

// Add a palette, or overwrite the existing one with the same name.
void save(const QString& name, const std::vector<QColor>& colors);

// Remove the palette at the given index (no-op if out of range).
void remove(int index);

// Generate a pleasant random palette of n colors, ordered light → dark.
std::vector<QColor> randomColors(int n);

// Extract a representative palette of n colors from an image via median-cut,
// ordered light → dark. Empty if the image is null/has no opaque pixels.
std::vector<QColor> fromImage(const QImage& img, int n);

} // namespace PaletteStore
