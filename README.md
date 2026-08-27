# OpenPDF Studio

A native Qt 6 PDF viewer and editor for Linux and Windows: read a document,
edit the text that is already in it, reorder its pages, annotate it, play and
embed media, and export it to PDF, Word or PNG. One application, no web stack.

Current version: **0.2.8**, early alpha. Work on copies of important documents
and check exported files with a second PDF viewer.

## Licensing

OpenPDF Studio Core is dual-licensed under **GPL-3.0-only** or the **OpenPDF
Studio Commercial License**.

Certain additional components, currently only `modules/rich-media/`, are
distributed under the **OpenPDF Studio Business License**. Those components are
free for personal use; commercial use requires a Business License after a
30-day evaluation period. The first start asks once which of the two this is,
personal preselected; personal use never asks for a key.

See [LICENSES/](LICENSES/) for details, and [LICENSES/README.md](LICENSES/README.md)
for the path-by-path map.

Everything you need to build, run and modify the full viewer and editor is
Core, and Core is free software. The PDF engine is PDFium under BSD-3-Clause,
so nothing in the dependency stack forces the GPL onto a distribution.

## Features

**Viewing**

* Several documents open at once, one tab each
* Single-page and grid view, thumbnail sidebar, presentation mode
* Bookmark panel: the document's outline, searchable, click to jump
* Zoom via toolbar or Ctrl + mouse wheel. Step size, zoom towards the pointer
  and the wheel action without a modifier are configurable
* Password-protected documents: the password is asked once and kept in memory
  for the renderer, editor and exporters; it is never written to disk
* Printing (needs `Qt6::PrintSupport`)

**Editing**

* Inline text editing: click a text run, type, and the change is written back
  into the page's content stream as vector text (needs qpdf)
* New text: drag a box with the Text tool and type into it
* Scanned pages fall back to OCR (needs Tesseract), so text can be edited there
  too
* Images: place a new one, move and scale it, or click an image already in the
  page to take it out of the content stream and treat it the same way
* Links: the *Attachment* tool puts a link area over selected text or a drawn
  rectangle, and edits or removes the ones already there
* Bookmarks: add one for the current page, rename, delete and reorder, written
  back into the document's outline (needs qpdf)
* Text selection with the Select tool, hover highlighting of what is editable
* Page organizer: reorder by drag & drop, rotate, delete, insert blank pages
  and merge further PDFs. Works on encrypted files as well
* Change history per document, with restore to an earlier state
* Edits to page structure go to a session working file; the file you opened is
  untouched until you save. Saving is atomic.

**Rich media** (`modules/rich-media/`, Business License)

* Finds `/RichMedia`, `/Screen` and `/Movie` annotations and plays them on the
  page, or hands them to an external player if you ask it to
* Embeds new video into a document, with a poster frame; a video dropped onto
  the document gets a page of its own
* Only embedded assets play, and the first playback in a document asks first
* Video that is not H.264 is caught before it is embedded and a conversion is
  offered (needs ffmpeg)
* Needs qpdf, and Qt Multimedia except on Windows, which uses DirectShow and
  MFPlay. See [modules/rich-media/README.md](modules/rich-media/README.md).

**Export**

* PDF: page ranges, user password, and switches for comments, form fields,
  embedded fonts and image compression (honouring them needs qpdf)
* Word `.docx`: text, layout and images
* PNG: one file per page, adjustable quality
* Estimated output size before you export

**Interface**

* Light and dark theme
* Configurable keyboard shortcuts
* The tool rail can be reordered and individual tools hidden
* Update check against the repository's tags, manually in Settings › Advanced
  or in the background at a chosen interval. It reports and links to
  <https://openpdf-studio.nullskill.de/download.html>, never downloads or
  installs anything, and can be switched off there
* 11 translations besides English: German, French, Spanish, Italian,
  Portuguese, Dutch, Polish, Russian, Chinese, Japanese, Korean

## Not there yet

* Text search. `Ctrl+F` has a shortcut and a settings entry, but no dialog
  behind it yet
* The *Draw*, *Table* and *Comment* tools are in the rail but do nothing; they
  fall back to Select
* Real PDF/A conformance. The PDF/A card in the export dialog currently
  produces an ordinary PDF
* Form editing, redaction, digital signatures
* Crash recovery from the session working files
* On the media side: playback in presentation mode, moving or resizing a medium
  already in the document, an export option to keep or drop media, and undo and
  change history for any of it

## Install

### Linux

Grab a package from the Releases page:

```bash
sudo dnf install openpdf-studio-0.2.8-1.x86_64.rpm      # Fedora / RHEL / openSUSE
sudo apt install ./openpdf-studio_0.2.8_amd64.deb       # Debian / Ubuntu
```

Or use the AppImage, which needs nothing installed:

```bash
chmod +x OpenPDF_Studio-0.2.8-x86_64.AppImage
./OpenPDF_Studio-0.2.8-x86_64.AppImage
```

### Windows

Run `OpenPDF-Studio-0.2.8-Setup.exe`, or unpack
`OpenPDF-Studio-0.2.8-win64-portable.zip` and start `OpenPDFStudio.exe`.
The Windows build carries PDFium and qpdf, so editing, export options and rich
media all work. Tesseract is not in it, so scanned pages cannot be OCR'd.

## Build from source

```bash
git clone https://github.com/NuIIskill/OpenPDF-Studio.git
cd OpenPDF-Studio
packaging/fetch-pdfium.sh  # the PDF engine, without it nothing opens
./build.sh                 # Release → build/OpenPDFStudio
./build.sh --appimage      # additionally packs an AppImage
```

`build.sh` honours `BUILD_DIR`, `BUILD_TYPE` and `JOBS`, and passes any other
argument through to CMake.

### Dependencies

Required: CMake ≥ 3.20, a C++20 compiler, and Qt 6 Core / Widgets / Gui /
Network (Network only for the update check; it is part of qtbase).

Everything else is optional and only changes what gets compiled in. The code
behind each `HAVE_*` define still builds when the dependency is missing:

| Dependency | Enables | Fedora package |
| --- | --- | --- |
| PDFium | the PDF engine: rendering, text, saving (`HAVE_PDFIUM`) | none, run `packaging/fetch-pdfium.sh` |
| qpdf | PDF export options, organizer save, bookmarks, media (`HAVE_QPDF`) | `qpdf-devel` |
| Qt Multimedia | media playback and posters, with qpdf (`HAVE_RICH_MEDIA`) | `qt6-qtmultimedia-devel` |
| Tesseract | OCR on scanned pages (`HAVE_TESSERACT`) | `tesseract-devel`, `tesseract-langpack-deu` |
| `Qt6::PrintSupport` | printing (`HAVE_QT_PRINT`) | part of `qt6-qtbase-devel` |
| `Qt6::LinguistTools` | the `update_translations` target | `qt6-qttools-devel` |
| ffmpeg | converting video to H.264 while embedding it | `ffmpeg` (runtime only) |

The rich-media module is the one case that is all or nothing: it needs qpdf,
and Qt Multimedia except on Windows. If one is missing, configure warns and
leaves the module out instead of building half of it.
`-DOPENPDF_RICH_MEDIA=OFF` skips it deliberately.

Without a PDF backend the application starts but shows nothing. CMake says so
during configure (`PDF backend: none`).

### Windows cross-build and packages

```bash
packaging/windows/build-qpdf-mingw.sh  # once: qpdf for mingw64 → third_party/
./build-win.sh                         # mingw64 + Qt6 cross-build → build-win/
packaging/windows/package-win.sh       # portable ZIP + NSIS installer → dist/
packaging/linux/package-linux.sh       # RPM + DEB via CPack → dist/
```

## Headless entry points

The binary can do a fair amount without a display, which is how exports,
editing and the dialogs are regression-tested:

```bash
OpenPDFStudio --export-pdf    in.pdf out.pdf [pages=1,3-4] [nocomments] [noforms] \
                                             [nofonts] [nocompress] [q=60] [pw=…] [srcpw=…]
OpenPDFStudio --export-docx   in.pdf out.docx [pages=1,3-4] [q=60] [nocompress] [srcpw=…]
OpenPDFStudio --export-images in.pdf out.png [pages=1,3-4] [q=85] [srcpw=…]
OpenPDFStudio --select-text   in.pdf [page=1] [from=x,y] [to=x,y] [srcpw=…]
OpenPDFStudio --apply-edit    in.pdf out.pdf at=x,y text=… [page=1] [preview=out.png] [srcpw=…]
OpenPDFStudio --apply-edit    in.pdf out.pdf field=Name text=Value
OpenPDFStudio --organize-save in.pdf out.pdf
OpenPDFStudio --shot-window          out.png in.pdf [dark] [edit=x,y]
OpenPDFStudio --shot-organizer       out.png in.pdf [dark]
OpenPDFStudio --shot-presentation    out.png in.pdf [page=N]
OpenPDFStudio --shot-export-dialog   out.png [word|image]
OpenPDFStudio --shot-history-dialog  out.png [dark]
OpenPDFStudio --shot-settings        out.png ["License Key"] [dark|light]
OpenPDFStudio --shot-license-notice  out.png [dark|light]
```

Coordinates are PDF points with the origin top left, the same as
`--select-text` reports. `OPENPDF_USAGE=business|personal` overrides the
recorded usage for the two licence shots, which report the state they find
rather than declaring one.

## Project layout

```
src/
  app/        infrastructure: config.ini, settings, safe writes, session,
              history, passwords
  drm/        business licence: state, settings page, the two notices
  engine/     document logic, no widgets: document/, edit/, ocr/, render/
  ui/         everything that is a widget: bars/, panels/, tools/, theme/,
              view/, edit/, settings/, organizer/, history/, export/,
              bookmarks/, widgets/
  3rdparty/   vendored (nanosvg)
modules/
  rich-media/ source-available module, Business License
```

`engine/` never includes `ui/` and never touches QWidget; `app/` never includes
`ui/`; `src/` never includes `modules/`, the module registers itself with the
Core through three small registers instead. Every folder under `ui/` names a
task rather than a shape. Includes are root-relative to `src/`, and each source
folder carries its own `CMakeLists.txt`. A new file is registered where it is
created, not in a central list. [CLAUDE.md](CLAUDE.md) has the rules in full;
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
