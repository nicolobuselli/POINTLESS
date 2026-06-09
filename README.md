# ULTRA_Ditherer

A desktop application for high-quality halftone rendering of images.  
Built with C++ and Qt6, targeting Windows.

---

## Features

- **Multiple halftone shapes** — Circle, Square, Star, Spark, Cross (X), Plus (+), and Custom SVG
- **Fine-grained parameter control** — grid size, gamma, jitter, opacity, symbol scale
- **Stroke support** — configurable width, corner radius, and color
- **Color modes** — solid fill color (with full color picker) or sampled from source image
- **Multi-symbol mode** — assign up to 4 different shapes to luminosity ranges
- **Export** — PNG, JPEG, or vector SVG output
- **Live preview** — real-time rendering on a background thread

---

## Screenshots

*(add screenshots to `docs/screenshots/` and link them here)*

---

## Building from source

### Requirements

| Tool | Version |
|------|---------|
| Visual Studio | 2022 (MSVC 17) |
| CMake | ≥ 3.22 |
| Qt | 6.6 or 6.7 |

Qt6 can be installed via the [Qt Online Installer](https://www.qt.io/download-qt-installer)  
or via [vcpkg](https://vcpkg.io):

```bash
vcpkg install qt6-base qt6-svg qt6-concurrent
```

### Build steps

```bash
# 1. Clone
git clone https://github.com/YOUR_USERNAME/ULTRA_Ditherer.git
cd ULTRA_Ditherer

# 2. Configure (adjust CMAKE_PREFIX_PATH to your Qt installation)
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"

# 3. Build
cmake --build build --config Release

# 4. The executable and all required Qt DLLs are in build/Release/
```

### Running

After building, run `build/Release/ULTRA_Ditherer.exe`.  
`windeployqt` is called automatically as a post-build step, so all DLLs are
already next to the executable — no additional installation needed.

---

## Project structure

```
ULTRA_Ditherer/
├── CMakeLists.txt
├── assets/
│   └── style.qss          Dark theme stylesheet
├── src/
│   ├── main.cpp
│   ├── resources.qrc
│   ├── core/
│   │   ├── HalftoneParams.h      Parameter struct (no Qt UI dependency)
│   │   ├── HalftoneRenderer.h
│   │   └── HalftoneRenderer.cpp  Pure rendering logic
│   ├── ui/
│   │   ├── MainWindow.*          Top-level window, orchestration
│   │   ├── PreviewWidget.*       Custom rendering preview (left panel)
│   │   └── ControlPanel.*        Scrollable parameter panel (right panel)
│   └── workers/
│       └── RenderWorker.*        Background thread via QThreadPool
└── .github/
    └── workflows/
        └── build.yml             CI: Windows MSVC build
```

---

## Dependencies

| Library | How included |
|---------|-------------|
| Qt 6 (Core, Gui, Widgets, Svg, Concurrent) | External — install separately |

No other dependencies. Qt's built-in `QPainter`, `QImage`, and `QSvgGenerator`
handle all rendering and file I/O.

---

## License

MIT — see [LICENSE](LICENSE)
