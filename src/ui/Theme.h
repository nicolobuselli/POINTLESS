#pragma once
#include "UiScale.h"

// ============================================================
//  Theme — the single source of truth for UI rhythm & chrome
//  constants used from C++ layout code.
//
//  All lengths are Figma px @2558 — pass them through Ui::px().
//  Colors and fonts of QSS-styled widgets live in
//  assets/style.qss (the other half of the design system);
//  the hex constants below exist for the few inline styles C++
//  has to build at runtime, and MUST match style.qss.
// ============================================================

namespace Ui {

// ── Column frame ────────────────────────────────────────────
inline constexpr int kColLeft         = 20;  // left edge of titles and controls
inline constexpr int kColRight        = 70;  // right icon gutter (eyes, +, loc dots)

// ── Vertical rhythm ─────────────────────────────────────────
inline constexpr int kTitleBandPadV   = 12;  // padding above/below a section title
inline constexpr int kGapTitleToFirst = 2;   // title band → first control of the section
inline constexpr int kGapLabelToCtrl  = 4;   // control name → its box/slider
inline constexpr int kGapRows         = 12;  // control → next control's name
inline constexpr int kGapTwinBoxes    = 12;  // two boxes side by side (X|Y, W|H, …)

// ── Boxes ───────────────────────────────────────────────────
inline constexpr int kBoxH       = 48;  // every standard input box
inline constexpr int kBoxRadius  = 8;
inline constexpr int kBoxFontPx  = 17;  // any text inside a box
inline constexpr int kCellW      = 58;  // slider value cell (kCellW × kBoxH)

// ── Hairlines ───────────────────────────────────────────────
// Every horizontal rule is exactly 1 *logical* px (setFixedHeight(1)),
// never Ui::px(1) — px() can round to 2 on large screens and the lines
// stop matching each other.

// ── Colors used from C++ inline styles (master copy: style.qss) ──
inline constexpr const char* kColBoxBg     = "#ff0000";
inline constexpr const char* kColBoxBorder = "#5D5D5D";
inline constexpr const char* kColBoxHover  = "#828282";
inline constexpr const char* kColBoxChecked= "#484848";
inline constexpr const char* kColText      = "#E3E3E3";
inline constexpr const char* kColLabel     = "#B2B2B2";
inline constexpr const char* kColValue     = "#A6A6A6";
inline constexpr const char* kColTitle     = "#EEEEEE";
inline constexpr const char* kColLine      = "#3B3B3B";

} // namespace Ui
