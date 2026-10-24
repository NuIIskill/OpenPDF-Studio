# Building

[README](../../README.md) · [Developer documentation](../../README.md#developer-documentation)

Build requirements are CMake 3.20 or later, a C++20 compiler and Qt 6 Core,
Widgets, Gui and Network. Optional libraries and the features they enable are
listed in [Dependencies](dependencies.md).

## Linux

From a shell with the required development packages installed:

```bash
git clone https://github.com/NuIIskill/OpenPDF-Studio.git
cd OpenPDF-Studio
packaging/fetch-pdfium.sh
./build.sh
./build/OpenPDFStudio
```

The PDFium fetch script downloads the pinned Linux and Windows x86_64
libraries into `third_party/pdfium/` and verifies their checksums. It requires
curl, tar and sha256sum. Without PDFium, the application can compile and start
but has no document functions.

`build.sh` configures and builds Release by default, producing
`build/OpenPDFStudio`. It accepts these environment variables:

| Variable | Default | Purpose |
| --- | --- | --- |
| `BUILD_DIR` | `build` | Build directory |
| `BUILD_TYPE` | `Release` | CMake build type |
| `JOBS` | `nproc` | Parallel build jobs |

Other arguments are passed to CMake. For example:

```bash
BUILD_DIR=build-debug BUILD_TYPE=Debug JOBS=4 ./build.sh -DOPENPDF_RICH_MEDIA=OFF
```

Equivalent direct CMake commands:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The native Linux binary defaults to Wayland unless `QT_QPA_PLATFORM` is
already set. A graphical launch needs the corresponding Qt platform plugin.
For headless checks, see [Testing](testing.md).

## Configure features

Optional dependencies are detected during configuration; read its output to
confirm PDFium, qpdf, OCR, digital signatures, document import and media.
Feature defines such as `HAVE_PDFIUM` are set by CMake detection, not by
passing them manually to the compiler.

- `-DPDFium_DIR=/path/to/pdfium` selects a directory containing
  `PDFiumConfig.cmake`. The default is `third_party/pdfium/linux-x64` on
  Linux and `third_party/pdfium/win-x64` on Windows.
- `-DOPENPDF_RICH_MEDIA=OFF` builds the Core without the media module. A
  checkout with the module directory absent also configures and builds.
- `-Dqpdf_DIR=/path/to/lib/cmake/qpdf` selects a qpdf CMake package when needed.

If qpdf or, on Linux, Qt Multimedia is missing, CMake warns and skips the whole
Rich Media module. Other optional features are likewise compiled according
to the libraries found. See [Dependencies](dependencies.md) for detection details.

## Windows cross-build

The repository provides a Linux-to-Windows x86_64 build using MinGW-w64 and
Qt 6 for Windows. On Fedora, the build script documents these toolchain packages:

```bash
sudo dnf install mingw64-filesystem mingw64-qt6-qtbase mingw64-qt6-qtsvg \
                 mingw64-qt6-qtimageformats
```

Then fetch PDFium, build qpdf for MinGW and build the application:

```bash
packaging/fetch-pdfium.sh
packaging/windows/build-qpdf-mingw.sh
./build-win.sh
```

qpdf goes into `third_party/mingw64/`; the executable and deployed runtime
files go into `build-win/`. Qt Multimedia is not required on Windows; the
media module uses DirectShow and MFPlay. Digital signing uses the Windows
certificate store through CNG.

OCR needs `mingw64-tesseract`. The build deploys German, English and French
language data from the host's `tesseract-langpack-deu`, `-eng` and `-fra`
packages. `TESSDATA_SRC` can override the default
`/usr/share/tesseract/tessdata` source directory. TIFF and WebP need
`mingw64-qt6-qtimageformats`.

`build-win.sh` accepts `BUILD_DIR`, `BUILD_TYPE`, `JOBS` and additional CMake
arguments. It detects a system MinGW/Qt toolchain; use
`TOOLCHAIN_FILE=/path/to/toolchain.cmake` to override it. Dependency deployment
uses the Fedora MinGW sysroot paths in the script, so another toolchain may
need deployment adjustments. Host pkg-config is deliberately excluded during
cross-compilation to avoid mixing Linux and Windows headers.

## Packages and translations

Use [Releasing](releasing.md) for RPM, DEB, AppImage, Windows ZIP and installer
creation, plus translation updates. For build failures or missing runtime
features, see [Troubleshooting](../user/troubleshooting.md).
