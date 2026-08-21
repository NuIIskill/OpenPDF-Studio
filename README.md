# OpenPDF Studio

A native Qt 6 PDF viewer and editor for Linux and Windows: read a document,
edit the text that is already in it, reorder its pages, annotate it, and export
it to PDF, Word or PNG - in one application, without a web stack.

Current version: **0.2.5** - early alpha. Work on copies of important
documents and check exported files with a second PDF viewer.

## Licensing

OpenPDF Studio Core is dual-licensed under **GPL-3.0-only** or the **OpenPDF
Studio Commercial License**.

Certain additional components - currently only `modules/rich-media/` - are
distributed under the **OpenPDF Studio Business License**. Those components are
free for personal use; commercial use requires a Business License after a
30-day evaluation period.

See [LICENSES/](LICENSES/) for details, and [LICENSES/README.md](LICENSES/README.md)
for the path-by-path map.

Everything you need to build, run and modify the full viewer and editor is
Core, and Core is free software. The PDF engine is PDFium under BSD-3-Clause,
so nothing in the dependency stack forces the GPL onto a distribution.

## Features

**Viewing**

* Single-page and grid view, thumbnail sidebar, presentation mode
* Zoom via toolbar or Ctrl + mouse wheel - step size, zoom-to-pointer and the
  no-modifier wheel action are configurable
* Password-protected documents: the password is asked once and kept in memory
  for the renderer, editor and exporters; it is never written to disk
* Printing (needs `Qt6::PrintSupport`)

**Editing**

* In-place text editing: click a text run, type, and the change is written back
  into the page's content stream as vector text (needs qpdf)
* Scanned pages fall back to OCR (needs Tesseract), so text can be edited there
  too
* Annotations: text boxes, comments and images, with undo/redo
* Text selection with the Select tool, hover highlighting of what is editable
* Page organizer: reorder by drag & drop, rotate, delete, insert blank pages
  and merge further PDFs - works on encrypted files as well
* Change history per document, with restore to an earlier state
* Edits to page structure go to a session working file; the file you opened is
  untouched until you save. Saving is atomic.

**Export**

* PDF - page ranges, user password, and switches for comments, form fields,
  embedded fonts and image compression (the option-aware path needs qpdf)
* Word `.docx` - text, layout and images
* PNG - one file per page, adjustable quality
* Estimated output size before you export

**Interface**

* Light and dark theme
* Configurable keyboard shortcuts
* 11 translations besides English: German, French, Spanish, Italian,
  Portuguese, Dutch, Polish, Russian, Chinese, Japanese, Korean

## Not there yet

* Real PDF/A conformance - the PDF/A card in the export dialog currently
  produces an ordinary PDF
* Rich media playback (`modules/rich-media/`) - media annotations are detected
  and protected from editing, but not played
* Form editing, redaction, digital signatures
* Crash recovery from the session working files

## Install

### Linux

Grab a package from the Releases page:

```bash
sudo dnf install openpdf-studio-0.2.5-1.x86_64.rpm      # Fedora / RHEL / openSUSE
sudo apt install ./openpdf-studio_0.2.5_amd64.deb       # Debian / Ubuntu
```

Or use the AppImage, which needs nothing installed:

```bash
chmod +x OpenPDF_Studio-0.2.5-x86_64.AppImage
./OpenPDF_Studio-0.2.5-x86_64.AppImage
```

### Windows

Run `OpenPDF-Studio-0.2.5-Setup.exe`, or unpack
`OpenPDF-Studio-0.2.5-win64-portable.zip` and start `OpenPDFStudio.exe`.
The Windows build ships without qpdf, so PDF export there cannot select page
ranges or set a password; Word and PNG export are unaffected.

## Build from source

```bash
git clone https://github.com/NuIIskill/OpenPDF-Studio.git
cd OpenPDF-Studio
./build.sh                 # Release → build/OpenPDFStudio
./build.sh --appimage      # additionally packs an AppImage
```

`build.sh` honours `BUILD_DIR`, `BUILD_TYPE` and `JOBS`, and passes any other
argument through to CMake.

### Dependencies

Required: CMake ≥ 3.20, a C++20 compiler, and Qt 6 Core / Widgets / Gui.

Everything else is optional and only changes what gets compiled in - the code
behind each `HAVE_*` define still builds when the dependency is missing:

| Dependency | Enables | Fedora package |
| --- | --- | --- |
| PDFium | the PDF engine: rendering, text, saving (`HAVE_PDFIUM`) | none - run `packaging/fetch-pdfium.sh` |
| qpdf | PDF export options, organizer save (`HAVE_QPDF`) | `qpdf-devel` |
| Tesseract | OCR on scanned pages (`HAVE_TESSERACT`) | `tesseract-devel`, `tesseract-langpack-deu` |
| `Qt6::PrintSupport` | printing (`HAVE_QT_PRINT`) | part of `qt6-qtbase-devel` |
| `Qt6::LinguistTools` | the `update_translations` target | `qt6-qttools-devel` |

Without a PDF backend the application starts but shows nothing - CMake says so
during configure (`PDF backend: none`).

### Windows cross-build and packages

```bash
./build-win.sh                        # mingw64 + Qt6 cross-build → build-win/
packaging/windows/package-win.sh      # portable ZIP + NSIS installer → dist/
packaging/linux/package-linux.sh      # RPM + DEB via CPack → dist/
```

## Headless entry points

The binary can do a few things without a display, which is how exports are
regression-tested:

```bash
OpenPDFStudio --export-pdf  in.pdf out.pdf [pages=1,3-4] [nocomments] [noforms] \
                                           [nofonts] [nocompress] [q=60] [pw=…] [srcpw=…]
OpenPDFStudio --export-docx in.pdf out.docx [pages=1,3-4] [q=60] [nocompress] [srcpw=…]
OpenPDFStudio --shot-window out.png in.pdf [dark] [edit=x,y]
OpenPDFStudio --shot-export-dialog out.png [word|image]
OpenPDFStudio --shot-history-dialog out.png [dark]
```

## Project layout

```
src/
  app/        infrastructure: settings, safe writes, session, history, passwords
  engine/     document logic, no widgets - edit/, ocr/, render/, document/
  ui/         everything that is a widget - bars/, panels/, dialogs/, tools/,
              view/, edit/, widgets/, theme/
  3rdparty/   vendored (nanosvg)
modules/
  rich-media/ source-available module, Business License
```

`engine/` never includes `ui/` and never touches QWidget; `app/` never includes
`ui/`. Includes are root-relative to `src/`. Each source folder carries its own
`CMakeLists.txt` - a new file is registered where it is created, not in a
central list. [CLAUDE.md](CLAUDE.md) has the rules in full;
[docs/](docs/) holds the architecture notes.

## Contributing

Bug reports, test documents, translations, UI work and pull requests are all
welcome. For anything larger, please open an issue first so the approach can be
discussed.

Contributions to the Core are accepted under its dual license: by submitting
one you grant the copyright holder the right to distribute it under both
GPL-3.0-only and the Commercial License, while keeping the copyright in your
own work. Without that the dual-licensing model could not hold.

## Security

Please do not attach private or sensitive PDFs to bug reports. If you find a
security issue, report it privately to nullskilll@gmail.com instead of opening
a public issue.
