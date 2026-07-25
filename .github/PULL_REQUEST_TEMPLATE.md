## What does this PR do?

<!-- Short description of the change -->

## Why?

<!-- Motivation / linked issue -->

## How was this tested?

<!-- Built and ran the app? Which OS? Which feature exercised? -->

## Screenshots (if UI change)

<!-- Before/after, especially for anything touching ControlsPanel/ModePanel/PreviewWidget -->

## Checklist

- [ ] Builds cleanly (`mingw32-make -j4`, no new warnings)
- [ ] Tested by running the app, not just compiling
- [ ] If new fields added to `Params.h` structs: `operator==` updated
- [ ] If new fields need persistence: `ProjectIO.cpp` `toJson`/`fromJson` updated
