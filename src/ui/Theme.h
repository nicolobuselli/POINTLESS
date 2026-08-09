#pragma once
#include "UiScale.h"
#include <QColor>

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

// Dialog cards are drawn from assets/icons/dialog_card*.svg exactly as
// exported. The dialog is sized to each file's own aspect ratio — anything
// else stretches the bubbly edge. The warning file carries transparent
// padding around its card (Figma's shadow bounds), hence the wider box and
// the bigger content margins that go with it.
inline constexpr int    kDialogWFigma     = 620;
inline constexpr double kDialogAspect     = 1267.0 / 583.0;
inline constexpr int    kDialogWarnWFigma = 643;
inline constexpr double kDialogWarnAspect = 1314.0 / 630.0;
inline constexpr int    kDialogWarnPad    = 12;   // transparent margin in that file

// ── Hairlines ───────────────────────────────────────────────
// Every horizontal rule is exactly 1 *logical* px (setFixedHeight(1)),
// never Ui::px(1) — px() can round to 2 on large screens and the lines
// stop matching each other.

// ── Palette (colors used directly in C++ paint code / inline styles) ────
// These mirror the @token values in main.cpp's substitutePaletteTokens(),
// which is what style.qss draws from. Anything painted with QPainter or set
// via setStyleSheet() in C++ (not routed through style.qss) must pull from
// here instead of a bare hex literal, or a palette change misses that spot.
inline const QColor kColBgWindow  {"#1E1E1E"};
inline const QColor kColBgPanel   {"#272727"};
inline const QColor kColSurface2  {"#3B3B3B"};   // hairlines, flat dark chrome
inline const QColor kColBoxStroke {"#3D3D3D"};   // default box border
inline const QColor kColBoxStrokeActive {"#45556C"}; // box border while editing / text selection
inline const QColor kColPopupBorder {"#5D5D5D"}; // floating popup/panel border

inline const QColor kColTextBody  {"#E3E3E3"};
inline const QColor kColTextTitle {"#EEEEEE"};
inline const QColor kColTextLabel {"#B2B2B2"};
inline const QColor kColTextDim   {"#8E8E8E"};
inline const QColor kColWhite     {"#FFFFFF"};

inline const QColor kColAccent       {"#FD5A1F"};
inline const QColor kColSelBlue      {"#568AD9"};
inline const QColor kColSelectStroke {"#A0C03F"};

// "Localize" per-parameter overlay accent (lime/olive family, canvas loc-dots
// + their checkbox/confirm button) — not part of the orange/blue system above.
inline const QColor kColLocLime      {"#D2FC51"};
inline const QColor kColLocLimeHover {"#DFFF7A"};
inline const QColor kColLocLimePress {"#B9DE3F"};
inline const QColor kColLocOliveDark   {"#2B3313"};
inline const QColor kColLocOliveStroke {"#607817"};
inline const QColor kColLocOliveStroke2{"#89A928"};

// Canvas overlay chrome (transform/selection handles, loc-dot rings).
inline const QColor kColCanvasHandle {"#F0F0F0"};

} // namespace Ui
