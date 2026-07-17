#pragma once
#include "UiScale.h"

// ============================================================
//  Theme — the single source of truth for UI rhythm & chrome
//  constants used from C++ layout code.
//
//  All lengths are Figma px @2558 — pass them through Ui::px().
//  Colors and fonts live entirely in assets/style.qss (the other
//  half of the design system), addressed by objectName — never
//  duplicate a color here, give the widget an objectName instead.
// ============================================================

namespace Ui {

// ── Column frame ────────────────────────────────────────────
inline constexpr int kColLeft         = 20;  // left edge of titles and controls
inline constexpr int kColRight        = 60;  // right icon gutter (eyes, +, loc dots)

// ── Vertical rhythm ─────────────────────────────────────────
inline constexpr int kTitleBandPadV   = 12;  // padding above/below a section title
inline constexpr int kGapTitleToFirst = 2;   // title band → first control of the section
inline constexpr int kGapLabelToCtrl  = 6;   // control name → its box/slider
inline constexpr int kGapRows         = 12;  // control → next control's name
inline constexpr int kGapTwinBoxes    = 18;  // two boxes side by side (X|Y, W|H, …)

// ── Boxes ───────────────────────────────────────────────────
inline constexpr int kBoxH       = 42;  // every standard input box (halved, then +30%)
inline constexpr int kBoxHFull   = 42;  // unhalved: Layers rows, Levels, Timeline, Library
inline constexpr int kBoxRadius  = 8;
inline constexpr int kBoxFontPx  = 17;  // any text inside a box
inline constexpr int kCellW      = 58;  // slider value cell (kCellW × kBoxH)

// ── Hairlines ───────────────────────────────────────────────
// Every horizontal rule is exactly 1 *logical* px (setFixedHeight(1)),
// never Ui::px(1) — px() can round to 2 on large screens and the lines
// stop matching each other.

} // namespace Ui
