# OpenPDF Studio

A native Qt 6 PDF viewer and editor for Linux and Windows. Read PDFs, edit
existing text, organize pages, annotate, redact, sign and export documents in one
desktop application. Word, OpenDocument and image files open as PDF working
copies, converted locally.

Current version: **0.3.5, alpha**. Saving replaces the original file, so keep a
backup of important PDFs and check the result in a second viewer.

![OpenPDF Studio displaying the NLBK test PDF, with editing tools on the left and document actions on the right](docs/images/screenshots/overview.png)

*The document view. See [Getting started](docs/user/getting-started.md).
Sample document: [NLBK Niedersachsen test PDF](https://www.nlbk.niedersachsen.de/download/164891/Test-pdf_3.pdf).*

## Key features

- Read several documents in tabs, search text, browse bookmarks and present pages;
  an empty tab lists the recently opened files.
- Edit text in place, with font, size, style and colour for a whole box or only
  the selected characters; add text and images, draw, highlight and leave notes.
- Edit scanned pages through OCR in up to 12 languages.
- Redact text, images and drawings for good, checked before the file is written.
- Reorder, rotate, insert, delete and merge pages, or append another file's pages,
  with undo, history and crash recovery.
- Place handwritten or digital signatures, see each signature's status on the page,
  and manage passwords and permissions; restricted PDFs follow their permissions.
- Export to PDF, Word, PNG or JPEG, and print with a preview, several pages per
  sheet or as a poster over 2, 4 or 9 sheets.
- Play, embed and open video and audio through the separately licensed Rich Media module.
- Add or remove OCR languages and ffmpeg in Settings > Add-on Packages, through
  the package manager on Linux and as a verified download on Windows.
- Choose light or dark themes, customize shortcuts and tools, and use English
  or one of 11 translations.

Feature availability depends on the components included in a build. PDF/A
export and a complete form editor are not available yet. The
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
sudo dnf install ./openpdf-studio-0.3.5-1.x86_64.rpm
sudo apt install ./openpdf-studio_0.3.5_amd64.deb
# Alternatively:
chmod +x OpenPDF_Studio-0.3.5-x86_64.AppImage
./OpenPDF_Studio-0.3.5-x86_64.AppImage
```

On Windows, run `OpenPDF-Studio-0.3.5-Setup.exe`, or extract
`OpenPDF-Studio-0.3.5-win64-portable.zip` and start `OpenPDFStudio.exe`.
The setup offers ffmpeg as an optional component. See
[Installation](docs/user/installation.md) for details.

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
| [Editing](docs/user/editing.md) | Text, scans, images, annotations, redaction, pages and media |
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
