# OpenPDF Studio

A native Qt 6 PDF viewer and editor for Linux and Windows. Read PDFs, edit
existing text, organize pages, annotate, sign and export documents in one
desktop application. Word, OpenDocument and image files open as PDF working
copies, converted locally.

Current version: **0.3.2, alpha**. Saving replaces the original file, so keep a
backup of important PDFs and check edited text in a second viewer.

## Key features

- Read several documents in tabs, search text, browse bookmarks and present pages.
- Edit text in place, add text and images, draw, highlight and leave notes; OCR
  in English, German and French supports editing scanned pages.
- Reorder, rotate, insert, delete and merge pages, with undo, history and crash recovery.
- Place handwritten or digital signatures and manage passwords and permissions.
- Export to PDF, Word, PNG or JPEG, and print with a preview, several pages per
  sheet or as a poster over 2, 4 or 9 sheets.
- Play and embed video and audio through the separately licensed Rich Media module.
- Choose light or dark themes, customize shortcuts and tools, and use English
  or one of 11 translations.

Feature availability depends on the components included in a build. PDF/A
export, redaction and a complete form editor are not available yet. The
[user documentation](#user-documentation) explains workflows and limitations.

## Supported operating systems

| System | Packages |
| --- | --- |
| Linux, x86_64 | RPM, DEB and AppImage; the AppImage requires a Wayland session |
| Windows, x86_64 | Installer and portable ZIP |

## Installation

Download a package from [GitHub Releases](https://github.com/NuIIskill/OpenPDF-Studio/releases)
or the [download page](https://openpdf-studio.nullskill.de/download.html).

On Linux, install the RPM or DEB with your package manager, or run the AppImage:

```bash
sudo dnf install ./openpdf-studio-0.3.2-1.x86_64.rpm
sudo apt install ./openpdf-studio_0.3.2_amd64.deb
# Alternatively:
chmod +x OpenPDF_Studio-0.3.2-x86_64.AppImage
./OpenPDF_Studio-0.3.2-x86_64.AppImage
```

On Windows, run `OpenPDF-Studio-0.3.2-Setup.exe`, or extract
`OpenPDF-Studio-0.3.2-win64-portable.zip` and start `OpenPDFStudio.exe`.
See [Installation](docs/user/installation.md) for details.

## Licensing

The **Core** is dual-licensed under **GPL-3.0-only** or the **OpenPDF Studio
Commercial License**, which requires a separate written agreement. The Core
contains the viewer, editor, annotations, organizer and exporters.

The **Rich Media module** is source-available under the **OpenPDF Studio
Business License**: personal use is free; business use requires a Business
License after a 30-day evaluation. See [Licensing](docs/user/licensing.md),
the [license overview](LICENSE) and the [licensing map](LICENSES/README.md)
for the terms and component boundaries.

## User documentation

| Guide | Contents |
| --- | --- |
| [Getting started](docs/user/getting-started.md) | First document, navigation, settings and saving |
| [Installation](docs/user/installation.md) | Linux packages, Windows setup and portable mode |
| [Editing](docs/user/editing.md) | Text, scans, images, annotations, pages and media |
| [Signatures](docs/user/signatures.md) | Handwritten and digital signatures, certificates and trust |
| [Export](docs/user/export.md) | PDF, Word, images and printing |
| [Licensing](docs/user/licensing.md) | Core, Rich Media, personal use and business licensing |
| [Troubleshooting](docs/user/troubleshooting.md) | Missing features, rendering, OCR, media and recovery |

## Developer documentation

| Guide | Contents |
| --- | --- |
| [Architecture](docs/dev/architecture.md) | Startup, document state, rendering, saving and recovery |
| [Building](docs/dev/building.md) | Linux builds, Windows cross-builds and CMake options |
| [Dependencies](docs/dev/dependencies.md) | Required libraries, optional features and build defines |
| [PDF engine](docs/dev/pdf-engine.md) | PDFium, coordinate spaces, editing and qpdf |
| [Rich media](docs/dev/rich-media.md) | Module integration, playback, embedding and deployment |
| [Testing](docs/dev/testing.md) | Headless commands, screenshots and regression checks |
| [Releasing](docs/dev/releasing.md) | Versions, translations, Linux and Windows packaging |

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines, code rules
and security reporting.
