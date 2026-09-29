# tests

The default Profile build now includes CTest checks. Use CMake presets with the
Qt 6.11.1 MinGW kit installed in C:/Qt:

    & C:/Qt/Tools/CMake_64/bin/cmake.exe --preset profile
    & C:/Qt/Tools/CMake_64/bin/cmake.exe --build --preset profile
    & C:/Qt/Tools/CMake_64/bin/ctest.exe --preset profile

Tests cover document safety/undo, frame bounds, project media and custom assets,
invalid input, animation timing and transform precision, SVG blend fidelity,
stale renders, CPU/GPU cache transitions, video round-trip/budgets/cancellation,
and the nearest-glyph oracle. The native GPU comparison skips explicitly if no
backend can initialize; a pixel mismatch fails. CPU checks never skip.

CI uses Qt 6.11.1/MSVC 2022, runs CTest and smoke-tests a staged package with
Qt/compiler directories removed from PATH. Runtime files are allowlisted;
manifest.json contains their SHA-256 hashes. Diagnostic symbols are separate.
The render_golden utility below remains a before/after image comparison; its
outputs are not an independently approved visual reference.

## render_golden.cpp — whole-pipeline regression differ

Renders a fixed set of mode configurations to PNG. Point it at two builds of
`src/core` and byte-compare the output: any refactor that claims "same pixels"
either proves it here or isn't done.

```sh
Q=C:/Qt/6.11.1/mingw_64
G=C:/Qt/Tools/mingw1310_64/bin/g++.exe
INC="-I$Q/include -I$Q/include/QtCore -I$Q/include/QtGui -I$Q/include/QtConcurrent -I$Q/include/QtSvg"
LIB="-L$Q/lib -lQt6Core -lQt6Gui -lQt6Concurrent -lQt6Svg"

# stage the "before" tree (e.g. from git) next to the current one, each with
# its own core/ subdir, then build one binary per tree:
"$G" -std=c++17 -O1 -I<tree> $INC -o render_<tree>.exe tests/render_golden.cpp <tree>/core/*.cpp $LIB

QT_QPA_PLATFORM=offscreen ./render_old.exe out_old
QT_QPA_PLATFORM=offscreen ./render_new.exe out_new
for f in out_old/*.png; do cmp -s "$f" "out_new/$(basename $f)" || echo "DIFF $f"; done
```

Covers Dot Grid / ASCII / Mosaic / Halftone in both the fixed-tone and
image-colour branches, the non-square ASCII lattice, the Ink halftone style,
three dither algorithms, and a full adjustment chain. Add a `shot(...)` when a
mode grows a branch this doesn't reach.

## nearest_glyph_check.cpp — ASCII glyph lookup

See the header comment in the file. Checks the breakpoint table against the
brute-force scan it replaced.

```sh
g++ -std=c++17 -O2 -o nearest_glyph_check tests/nearest_glyph_check.cpp && ./nearest_glyph_check
```

## export_frame_check.cpp — raster export bounds

Builds a one-layer document whose source is larger than its frame and verifies
both the exact output dimensions and the centre crop performed by the document
renderer. This protects still-image export from accidentally saving the raw
source dimensions again.
