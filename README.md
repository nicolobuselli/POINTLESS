<div align="center">

# ULTRATOOL

**A real-time halftone, dithering, and ASCII-art renderer for images and video.**

[![License: GPLv3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)](#download)
[![Built with Qt6](https://img.shields.io/badge/built%20with-Qt6-41cd52.svg)](#build-from-source)

ULTRATOOL turns photos and footage into dot screens, error-diffusion dithers,
ASCII glyph art, mosaic tile grids, and canonical 4-color CMYK halftone
separations — live, with layers, blend modes, per-parameter localization, and
a full keyframe animation timeline. It's a native C++/Qt6 desktop app: no
browser, no subscription, no upload.

<img width="800" alt="ULTRATOOL screenshot" src="https://github.com/user-attachments/assets/4789eec9-e86a-4976-8031-69a1d17c26db" />

</div>

---

## Table of contents

- [Why](#why)
- [Features](#features)
  - [Rendering modes](#five-rendering-modes)
  - [Color](#color)
  - [Animation & video](#animation--video)
  - [Compositing](#compositing)
  - [Project files](#project-files)
- [Download](#download)
- [Build from source](#build-from-source)
- [License](#license)

---

## Why

Most dithering/halftone tools are either a one-shot filter (apply once, done)
or a web toy with no layers and no video support. ULTRATOOL treats these
effects as a real compositing pipeline: stack multiple rendering modes as
layers, blend them, mask and scale individual parameters over regions of the
canvas, animate any of it on a timeline, and export straight to mp4.

---

## Features

### Five rendering modes

**🔵 Dot Grid** — Area-corrected dot screen: multiple point shapes, jitter, localization.

<img src="https://github.com/user-attachments/assets/a59f5232-4725-46c2-ae05-d374a1b34fde" alt="Dot Grid" width="800">

<br><br>

**🎨 Halftone** — Canonical AM halftone: 4 separate CMYK screens at their own angles, dot/hole inversion past 50% coverage.

<img src="https://github.com/user-attachments/assets/88204f81-2451-4009-bd89-6e0a42daed70" alt="Halftone" width="800">

<br><br>

**⚡ Dither** — Error diffusion, ordered, and threshold dithering, with palette-constrained color matching.

<img src="https://github.com/user-attachments/assets/cbbd21e1-5361-4bf1-bf03-27eae6ba0a6c" alt="Dither" width="800">

<br><br>

**🔤 ASCII** — Glyph-based rendering driven by real font metrics, not a lookup table.

<img src="https://github.com/user-attachments/assets/c9b6eb35-073a-437b-8912-c2c2fd746795" alt="ASCII" width="800">

<br><br>

**🧩 Mosaic** — Rectangular tile grid, one solid color or character per tone.

<img src="https://github.com/user-attachments/assets/7979ed5a-7e5f-46db-a40d-2224c17e3e53" alt="Mosaic" width="800">

<br><br>

Every mode shares the same layer stack, blend modes, and localization
system — so you can combine a Halftone base with an ASCII overlay, or
animate a Dither layer's threshold while a Mosaic layer sits underneath
at reduced opacity.

### Color

ULTRATOOL gives you two ways to handle color, switchable per layer:

**Original colors** — the render keeps the source image's own colors.
Coverage, dot placement, glyph selection, and every other quantity are still
computed correctly (in linear light — see below), but no color substitution
happens.

**Palette-constrained rendering** — every pixel is instead matched to the
nearest color in a palette of **1 to 8 colors**, using **OkLab** distance
rather than naive RGB distance. OkLab is perceptually uniform, so "nearest
color" actually means *nearest as your eye sees it* — matches don't skew
toward whichever channel happens to have the widest numeric range, which is
the usual failure mode of RGB or even Lab-based matching on saturated
palettes.

Where the palette comes from is up to you:

- **Extract from an image** — median-cut quantization pulls a representative
  N-color palette straight out of any photo, so you can "borrow" the palette
  of one image and apply it to another.
- **Generate randomly** — instantly roll a new random palette of 1–8 colors
  for quick exploration; keep re-rolling until something clicks.
- **Build by hand** — pick colors directly.
- **Save as a preset** — any palette, however you arrived at it, can be
  saved and recalled later, so a duotone or CMYK-esque palette you like
  becomes reusable across projects instead of a one-off.

Under the hood, all of this sits on top of gamma-correct color math: linear
light is used for quantities like coverage, dot area, and error diffusion,
while perceptual luma drives tone and glyph selection — so a 2-color
palette dither doesn't come out darker or lighter than it should just
because of how the math handles gamma.

<img width="800" alt="Color palette panel" src="https://github.com/user-attachments/assets/c521426c-9899-430c-b295-88a83d3065b7" />

### Animation & video

- Keyframe timeline (dopesheet UI, auto-key, easing presets, copy/paste
  keys) across more than 50 numeric parameters.
- mp4 import/export via bundled FFmpeg; PNG sequence import.

<div align="center">
<img src="https://github.com/user-attachments/assets/820c0159-a265-4014-ada1-840e322611b9" alt="Animation timeline" width="800">
</div>

### Compositing

- Layers with Photoshop-style blend modes, visibility, reordering, lock,
  per-layer transform (position, rotation, scale, flip).
- Per-parameter localization: mask and independently scale any of 14
  parameters over a hand-drawn region of the canvas.
- Gamma-correct color math throughout — linear light for quantities
  (coverage, dot area, error diffusion), perceptual luma for tone/glyph
  selection.

### Project files

- Full session save/load (`.ultra`, JSON) — frame, layers, transforms,
  animation, and an embedded still-image library.

---

## Download

Grab the latest Windows build from the
[Releases page](https://github.com/nicolobuselli/ULTRATOOL/releases/latest):
download the zip, extract it anywhere, and run `ULTRATOOL.exe`. No Qt,
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
