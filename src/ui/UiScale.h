#pragma once
#include <cmath>

// ============================================================
//  Global UI scale
//
//  The interface is designed in Figma at a reference width of
//  kDesignWidth px. On screen we scale every dimension (fonts,
//  paddings, widths) by S = windowWidth / kDesignWidth, so the
//  layout reproduces the design proportionally on any display /
//  DPI and stays responsive.
// ============================================================

namespace Ui {

inline constexpr double kDesignWidth = 2558.0;

inline double& scaleRef() { static double s = 1.0; return s; }
inline double  scale()    { return scaleRef(); }
inline void    setScale(double s) { scaleRef() = (s > 0.05 ? s : 1.0); }

// Scale a Figma-px measurement to on-screen logical px.
inline int px(double figmaPx) { return int(std::lround(figmaPx * scaleRef())); }

} // namespace Ui
