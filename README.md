# POINTLESS

**A real-time halftone, dithering, and ASCII-art renderer for images and video.**

POINTLESS turns photos and footage into dot screens, error-diffusion dithers,
ASCII glyph art, mosaic tile grids, and canonical 4-color CMYK halftone
separations — live, with layers, blend modes, per-parameter localization, and
a full keyframe animation timeline. It's a native C++/Qt6 desktop app: no
browser, no subscription, no upload.

<!-- ![POINTLESS screenshot](assets/screenshot.png) -->

## Why

Most dithering/halftone tools are either a one-shot filter (apply once, done)
or a web toy with no layers and no video support. POINTLESS treats these
effects as a real compositing pipeline: stack multiple rendering modes as
layers, blend them, mask and scale individual parameters over regions of the
canvas, animate any of it on a timeline, and export straight to mp4.

## Features

**Five rendering modes**
| Mode | What it does |
|---|---|
| **Dot Grid** | Area-corrected dot screen — multiple point shapes, jitter, localization |
| **Halftone** | Canonical AM halftone — 4 separate CMYK screens at their own angles, dot/hole inversion past 50% coverage |
| **Dither** | Error diffusion, ordered, and threshold dithering, with palette-constrained color matching |
| **ASCII** | Glyph-based rendering driven by real font metrics, not a lookup table |
| **Mosaic** | Rectangular tile grid, one solid color or character per tone |

**Compositing**
- Layers with Photoshop-style blend modes, visibility, reordering, lock,
  per-layer transform (position, rotation, scale, flip).
- Per-parameter localization: mask and independently scale any of 14
  parameters over a hand-drawn region of the canvas.
- Gamma-correct color math throughout — linear light for quantities
  (coverage, dot area, error diffusion), perceptual luma for tone/glyph
  selection.

**Color**
- Palette-constrained dithering with OkLab nearest-color matching.
- Median-cut palette extraction straight from an image.

**Animation & video**
- Keyframe timeline (dopesheet UI, auto-key, easing presets, copy/paste
  keys) across ~50 numeric parameters.
- mp4 import/export via bundled FFmpeg; PNG sequence import.

**Project files**
- Full session save/load (`.less`, JSON) — frame, layers, transforms,
  animation, and an embedded still-image library.

## Download

Grab the latest Windows build from the
[Releases page](https://github.com/nicolobuselli/ULTRATOOL/releases/latest):
download the zip, extract it anywhere, and run `POINTLESS.exe`. No Qt,
CMake, or other install required — the zip already bundles the Qt DLLs and
FFmpeg.

## Build from source

Only needed if you want to modify the code. Requires **Qt 6** (Core, Gui,
Widgets, Svg, Concurrent) and **CMake ≥ 3.22**. Windows only for now (video
export and the app icon rely on Win32 APIs).

```bash
cmake -B build -DCMAKE_PREFIX_PATH="<path-to-Qt6-install>"
cmake --build build --config Release
```

On Windows, `windeployqt` runs automatically as a post-build step to bundle
the Qt DLLs. `ffmpeg.exe` isn't committed to the repo (it's a ~100MB static
binary); run `tools/fetch-ffmpeg.ps1` once to download it into `tools/`
before building, and the build will bundle it next to the executable so
video import/export works out of the box.

CI (`.github/workflows/build.yml`) builds with MSVC + Qt 6.7 on every push;
see that file for a known-working toolchain if local setup gives trouble.

## License

GPLv3 — see [LICENSE](LICENSE). Third-party components and their licenses
are listed in [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt).
