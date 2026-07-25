# Contributing to POINTLESS

Thanks for considering a contribution.

## Reporting bugs / requesting features

Open an issue using the appropriate template. Include OS version, steps to
reproduce, and screenshots/screen recordings where relevant — this is a
visual tool, so a picture of the wrong output is usually the fastest way to
explain a bug.

## Building from source

POINTLESS is a Qt 6 (C++/Widgets) desktop app built with CMake, using the
MinGW toolchain on Windows. See the README's "Build from source" section for
prerequisites and steps.

## Making changes

1. Fork the repo and create a branch off `main`.
2. Keep changes focused — one feature/fix per PR.
3. Match the existing code style (see `CLAUDE.md` for architecture and UI
   conventions if you're touching the UI layer).
4. Test your change by actually running the app, not just building it.
5. Open a PR against `main` using the PR template, describing what changed
   and why.

## Scope

Current product priorities (see `CLAUDE.md` §11) are, in order: output
quality of existing modes, palette dithering completeness, then UX polish.
New rendering modes are not a current priority — if you want to propose one,
open an issue first to discuss before investing time in a PR.

## License

By contributing, you agree your contributions are licensed under the
project's GPLv3 license (see `LICENSE`).
