# ULTRATOOL

Real-time halftone & dithering image/video renderer. C++/Qt6 desktop app
with layers, live preview, and a dark-themed control panel.

## Features

- Four rendering modes: **Halftone** (area-corrected dot screens), **Dither**
  (error diffusion / ordered / threshold), **ASCII** (glyph-based, real font
  metrics), **Mosaic** (tonal tile grid).
- Layers with Photoshop-style blend modes, per-layer transform (position,
  rotation, scale, flip), visibility, reordering, lock.
- Per-parameter localization: mask and scale any of 14 parameters over a
  region of the canvas.
- Palette-constrained dithering with OkLab nearest-color matching and
  median-cut palette extraction from an image.
- Keyframe animation timeline (dopesheet, auto-key, easing presets) and mp4
  video import/export (bundled FFmpeg).
- Gamma-correct color math throughout (linear light for quantities,
  perceptual luma for tone/glyph selection).

## Build

Requires **Qt 6** (Core, Gui, Widgets, Svg, Concurrent) and **CMake ≥ 3.22**.
Windows only for now (video export and the app icon rely on Win32 APIs).

```bash
cmake -B build -DCMAKE_PREFIX_PATH="<path-to-Qt6-install>"
cmake --build build --config Release
```

On Windows, `windeployqt` runs automatically as a post-build step to bundle
the Qt DLLs, and `tools/ffmpeg.exe` is copied next to the executable so
video import/export works out of the box.

CI (`.github/workflows/build.yml`) builds with MSVC + Qt 6.7 on every push;
see that file for a known-working toolchain if local setup gives trouble.

## License

GPLv3 — see [LICENSE](LICENSE). Third-party components and their licenses
are listed in [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt).
