# ULTRATOOL

**A real-time halftone, dithering, and ASCII-art renderer for images and video.**

ULTRATOOL turns photos and footage into dot screens, error-diffusion dithers,
ASCII glyph art, mosaic tile grids, and canonical 4-color CMYK halftone
separations — live, with layers, blend modes, per-parameter localization, and
a full keyframe animation timeline. It's a native C++/Qt6 desktop app: no
browser, no subscription, no upload.

<img width="2560" height="1502" alt="image" src="https://github.com/user-attachments/assets/4789eec9-e86a-4976-8031-69a1d17c26db" />


## Why

Most dithering/halftone tools are either a one-shot filter (apply once, done)
or a web toy with no layers and no video support. ULTRATOOL treats these
effects as a real compositing pipeline: stack multiple rendering modes as
layers, blend them, mask and scale individual parameters over regions of the
canvas, animate any of it on a timeline, and export straight to mp4.

## Features

**Five rendering modes**
| Mode | What it does |
|---|---|
| **Dot Grid** | Area-corrected dot screen — multiple point shapes, jitter, localization |
<img width="800" height="450" alt="DotGrid_gif" src="https://github.com/user-attachments/assets/a59f5232-4725-46c2-ae05-d374a1b34fde" />

| **Halftone** | Canonical AM halftone — 4 separate CMYK screens at their own angles, dot/hole inversion past 50% coverage |
<img width="800" height="450" alt="halftone_gif" src="https://github.com/user-attachments/assets/88204f81-2451-4009-bd89-6e0a42daed70" />

| **Dither** | Error diffusion, ordered, and threshold dithering, with palette-constrained color matching |
<img width="800" height="450" alt="dither_gif" src="https://github.com/user-attachments/assets/cbbd21e1-5361-4bf1-bf03-27eae6ba0a6c" />

| **ASCII** | Glyph-based rendering driven by real font metrics, not a lookup table |
<img width="800" height="450" alt="ascii_gif" src="https://github.com/user-attachments/assets/c9b6eb35-073a-437b-8912-c2c2fd746795" />

| **Mosaic** | Rectangular tile grid, one solid color or character per tone |
<img width="800" height="450" alt="mosaic_gif" src="https://github.com/user-attachments/assets/7979ed5a-7e5f-46db-a40d-2224c17e3e53" />



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
- Full session save/load (`.ultra`, JSON) — frame, layers, transforms,
  animation, and an embedded still-image library.

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
