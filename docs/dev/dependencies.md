# Dependencies

[README](../../README.md) · [Developer documentation](../../README.md#developer-documentation)

The root [CMakeLists.txt](../../CMakeLists.txt) is the source of truth for
dependency detection and compile definitions. [Building](building.md) gives
the build commands; [Releasing](releasing.md) covers runtime packaging.

## Required toolchain

- CMake 3.20 or later and a C++20 compiler.
- Qt 6 Core, Widgets, Gui and Network. Network is used for the update check
  and is part of qtbase.

PDFium is optional to CMake but required for a usable document application.
Without it the window starts, while PDF rendering, text and document functions
are absent. There is no alternate PDF backend.

## Feature dependencies

The Fedora names below are those used by the repository's build guidance.
Other distributions use their own development package names.

| Dependency | Definition or target | Enables | Fedora package or source |
| --- | --- | --- | --- |
| PDFium | `HAVE_PDFIUM`, `HAVE_PDF_RENDERING` | PDF opening, rendering, text access and saving | `packaging/fetch-pdfium.sh` |
| qpdf | `HAVE_QPDF` | Preserving original content, PDF export options, vector organizer output, bookmarks, protection and media PDF objects | `qpdf-devel` |
| qpdf plus GnuTLS and p11-kit | `HAVE_DIGITAL_SIGNATURE` on Linux | Certificate providers, signing and verification | `gnutls-devel`, `p11-kit-devel` |
| qpdf plus Windows CNG | `HAVE_DIGITAL_SIGNATURE` on Windows | Windows certificate store and signing | Windows system libraries |
| qpdf plus Qt Multimedia, with module present and enabled | `HAVE_RICH_MEDIA` on Linux | Media reading, writing, playback and posters | `qt6-qtmultimedia-devel` |
| qpdf plus Windows media components, with module present and enabled | `HAVE_RICH_MEDIA` on Windows | Media module using DirectShow and MFPlay | Windows system components |
| zlib | `HAVE_ZLIB` | `.docx` and `.odt` import | `zlib-devel` |
| Tesseract and Leptonica | `HAVE_TESSERACT` | OCR for scans | `tesseract-devel` and language data |
| Qt PrintSupport | `HAVE_QT_PRINT` | Printing | Part of `qt6-qtbase-devel` |
| Qt LinguistTools | `update_translations` | Updating translation sources on native builds | `qt6-qttools-devel` |

All guarded code must compile when its dependency is absent. Rich Media is
enabled by default when its directory and dependencies are available; it is
skipped with a warning if required libraries are missing. Disable it explicitly
with `-DOPENPDF_RICH_MEDIA=OFF`.

## Runtime components

| Component | Purpose |
| --- | --- |
| Tesseract language data | OCR uses `deu+eng+fra` where available and falls back to `eng`; Fedora packages are `tesseract-langpack-deu`, `-eng` and `-fra` |
| Qt image format plugins | TIFF and WebP import; Fedora `qt6-qtimageformats`, Windows cross-build `mingw64-qt6-qtimageformats` |
| Qt Multimedia backend plugins | Actual Linux playback and decoding; shipping only the Qt Multimedia library is insufficient |
| ffmpeg executable | Video conversion on Linux and fallback or remuxing on Windows, where the packages ship an LGPL build (`packaging/windows/build-ffmpeg-mingw.sh`); not needed to open a PDF |
| Wayland platform plugin and client libraries | Default Linux display platform; required by the packaged AppImage |

Windows uses system H.264/AAC encoders when available. Media Foundation DLLs
are loaded at runtime, allowing the application to start on Windows N even
when those components are missing. See [Rich media](rich-media.md).

## Discovery and vendored code

PDFium is fetched as a pinned binary distribution into `third_party/`, which
is ignored by Git. qpdf is found through its CMake config, with pkg-config as
a native-build fallback. Native Tesseract uses pkg-config, while the cross-build
uses its CMake package. GnuTLS and p11-kit are detected through pkg-config on Linux.

`src/3rdparty/` contains nanosvg for icon rendering and Monocypher for license
key verification. A Linux build also links the system FreeType ahead of
PDFium's exported copy when found, keeping Qt font rendering separate from
PDFium's internal FreeType calls.

Third-party license texts and path boundaries are in the
[licensing map](../../LICENSES/README.md). PDFium's collected notices are in
[PDFIUM.txt](../../LICENSES/PDFIUM.txt); Tesseract, language-data and Leptonica
notices are in [TESSERACT.txt](../../LICENSES/TESSERACT.txt).
