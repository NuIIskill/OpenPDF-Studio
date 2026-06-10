# AppImage Packaging

This directory contains tooling and notes for producing a portable
AppImage bundle of OpenPDF Studio.

## Prerequisites

- `linuxdeploy` with the Qt plug-in
- `appimagetool`
- A release build of `OpenPDFStudio`

## Steps

```bash
# 1. Build a Release binary
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-release --parallel
DESTDIR=AppDir cmake --install build-release

# 2. Deploy Qt dependencies
linuxdeploy --appdir AppDir \
            --plugin qt \
            --output appimage

# 3. The resulting OpenPDFStudio-x86_64.AppImage is self-contained.
```

## Wayland note

The AppImage sets `QT_QPA_PLATFORM=wayland` at startup via the application
binary itself (`qputenv` in `main.cpp`).  Ensure the target system has
`libwayland-client` and a running Wayland compositor.
