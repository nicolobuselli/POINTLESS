<div align="center">

# POINTLESS

**A real-time halftone, dithering, and ASCII-art renderer for images and video.**

[![License: GPLv3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)](#download)
[![Built with Qt6](https://img.shields.io/badge/built%20with-Qt6-41cd52.svg)](#build-from-source)

POINTLESS turns photos and footage into dot screens, error-diffusion dithers,
ASCII glyph art, mosaic tile grids, and canonical 4-color CMYK halftone
separations — live, with layers, blend modes, per-parameter localization, and
a full keyframe animation timeline. It's a native C++/Qt6 desktop app: no
browser, no subscription, no upload.

<img width="2560" height="1500" alt="image" src="https://github.com/user-attachments/assets/7fcadd7d-ff37-4eeb-98c7-fe97e20289d1" />



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
or a web toy with no layers and no video support. POINTLESS treats these
effects as a real compositing pipeline: stack multiple rendering modes as
layers, blend them, mask and scale individual parameters over regions of the
canvas, animate any of it on a timeline, and export straight to mp4.

---

## Features

### Five rendering modes

**🔵 Dot Grid** — Area-corrected dot screen: multiple point shapes, jitter, localization.

<img width="2560" height="1500" alt="image" src="https://github.com/user-attachments/assets/1a0f9566-167a-4985-a09d-9c56524eb71d" />


<br><br>

**🎨 Halftone** — Canonical AM halftone: 4 separate CMYK screens at their own angles, dot/hole inversion past 50% coverage.

<img width="2560" height="1498" alt="image" src="https://github.com/user-attachments/assets/18069605-a53f-4072-a72e-dc565db8d146" />


<br><br>

**⚡ Dither** — Error diffusion, ordered, and threshold dithering, with palette-constrained color matching.

<img width="2560" height="1504" alt="image" src="https://github.com/user-attachments/assets/c0f1dfde-7972-428e-a83a-46c38cb0a935" />


<br><br>

**🔤 ASCII** — Glyph-based rendering driven by real font metrics, not a lookup table.

<img width="2558" height="1504" alt="image" src="https://github.com/user-attachments/assets/7a84d257-1a66-4272-bc5c-e7de09764c45" />


<br><br>

**🧩 Mosaic** — Rectangular tile grid, one solid color or character per tone.

<img width="2560" height="1498" alt="image" src="https://github.com/user-attachments/assets/ff3e556c-6b98-49e2-842b-554ea8ad9c03" />


<br><br>

Every mode shares the same layer stack, blend modes, and localization
system — so you can combine a Halftone base with an ASCII overlay, or
animate a Dither layer's threshold while a Mosaic layer sits underneath
at reduced opacity.

### Color

POINTLESS gives you two ways to handle color, switchable per layer:

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

<img width="2560" height="1504" alt="image" src="https://github.com/user-attachments/assets/80e00a94-3967-4a22-9ac9-30589064a4c3" />


### Animation & video

- Keyframe timeline (dopesheet UI, auto-key, easing presets, copy/paste
  keys) across more than 50 numeric parameters.
- mp4 import/export via bundled FFmpeg; PNG sequence import.

<div align="center">
<img width="2560" height="1496" alt="image" src="https://github.com/user-attachments/assets/c820d8e8-e0d6-4454-b8c4-5a1f38c942f9" />

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

- Full session save/load (`.less`, JSON) — frame, layers, transforms,
  animation, and an embedded still-image library.

---

## Download

Grab the latest Windows build from the
[Releases page](https://github.com/nicolobuselli/POINTLESS/releases/latest):
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
