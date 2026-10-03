# OpenPDF Studio architecture

How the parts of the program work together. The folder layout, the layer rules
and the build are in [CLAUDE.md](../CLAUDE.md).

## Startup

`main.cpp` applies theme and language from `config.ini`, then hands the
arguments to `Cli::run()`. A recognised command runs without a window and
exits. Otherwise `App` builds the `MainWindow`, opens a file named on the
command line, and `MainWindow::restoreSession()` offers what a crashed run
left behind. Started with `--render-server`, the program is a render helper of a
running window (see `RenderPool`) and does nothing else.

## Main window

`MainWindow` owns the frame: `TopToolbar`, `FormatBar`, `DrawBar`,
`LeftSidebar`, `BookmarkPanel`, the tool panels, `RightSidebar` and
`StatusBar`. Every tab is one `DocumentView` in a `QStackedWidget`.

The window only forwards. Bars and panels act on the current `DocumentView`,
and the view's signals update bars and panels. Work that runs through a dialog
lives in its task folder: `Exporting` and `Printing` in `ui/export/`,
`HistoryDialog::openFor()` in `ui/history/`, `DigitalSigning` in `ui/sign/`.

## One open document

`DocumentView` coordinates one document. The state sits in these parts:

| part | holds |
| --- | --- |
| `DocumentSource` (`engine/document/`) | the `PdfBackend`, the `PdfRenderer`, the `ContentProvider` and the `DocumentWorker` for the file currently loaded |
| `EditSession` (`engine/edit/`) | pending text, image and annotation edits |
| `DocumentJournal` (`engine/historymanager/`) | change log, unsaved state, save target and restore |
| `EditController` (`ui/edit/`) | the one edit open in the inline editor |
| controllers in `ui/view/` | page layout, zoom, find, text selection, hover |
| layers in `ui/view/`, `ui/notes/`, `ui/draw/` | images, links, notes, drawings, signatures |
| `PageOverlay`s from modules | whatever a module lays over the pages |

## Backend and edits

`PdfBackend` is the interface and `PdfiumBackend` its only implementation.
`PdfBackend::renderPage(page, scale, session)` draws a page with the session's
edits applied by `PdfiumEdits::applyToPage()`, which swaps text objects in the
page's object list in memory. PDFium only writes that list back on
`FPDFPage_GenerateContent()`, so `PdfiumWriter` runs the same function when
saving, and the screen shows exactly what the file will contain.

A character the edited text's font cannot encode is written in an embedded
system font (`PdfiumFonts::fallbackFont()`). The inline editor gets its glyph
widths from the same code (`PdfBackend::editMetrics()`) and lays text out with
the same `TextLayout` as `PdfiumEdits`, so lines, list markers and positions in
the editor are the ones written; soft line breaks travel in the text as U+2028.
An edit that keeps its style and box is laid out anchored to the original
lines (`PdfBackend::originalLines()`): unchanged lines stay the original PDF
objects and changed lines keep their text up to the first change in place.
PDFium drops text in Type3 fonts when it regenerates a page, so
`PdfiumWriter` takes those objects out first and `Type3Text` (qpdf) appends
their original operators as a separate content stream after the save.

## Rendering

### Where renders run

PDFium is not thread-safe and renders one page at a time per process. Every
call into it holds `PdfiumLock`; the GUI thread always gets the lock next, and
a background render pauses at the next point PDFium allows when it waits.

`PageLayoutEngine` plans what to render and hands it to the document's
`DocumentWorker`, a background thread. For an expensive document (a file of
32 MB or more, or two renders over 120 ms) the worker also gets `RenderPool`
(`engine/document/`): helper processes, the program itself started with
`--render-server` (`RenderServer`), each with its own PDFium and the file open.
They render unedited pages in parallel and send them back over a local socket.
Pages with edits, the search, links, notes and saving stay on the worker
thread. Before the file may be replaced, the helpers close it
(`DocumentWorker::stop()`).

The program adapts to the machine:

| situation | what happens |
| --- | --- |
| small document | no helpers; the worker renders everything |
| 1 to 3 cores | no helpers |
| more cores | half of them less one as helpers, at most 6 |
| little free memory | fewer helpers, about 400 MB free each |
| helpers do not start | the worker renders everything |
| 1 or 2 cores | only the nearest zoom levels are prepared |

The worker thread and the helpers run at a lower priority than the window, so
on few cores the window gets the processor first and pages arrive in the
pauses. Page renders and kept tiles share a quarter of the free memory,
between 192 MB and 2 GB (`app/SystemMemory`); prepared zoom levels get a quarter of
that again.

### Pages and tiles

The worker renders the visible pages first, nearest the middle of the view.
Pages are `PageView`s, which draw their picture as it is; a QLabel would
rescale it on every paint. A small preview of each page stands in while the
page is rendered again. A page larger than about four million device pixels
at the current zoom gets a render capped at eight million and, from
`PageTiles`, sharp tiles of 512 device pixels in a fixed grid over its visible
part. While a visible page shows nothing yet, `LoadingSpinner` turns over the
document.

Background renders keep their last pages loaded, so the tiles of a page share
its decoded images; an edited page is kept with its edits for as long as its
version stays the same. PDFium decodes an image only as finely as the first
render of it needs, so before the first part of a page is rendered, the page is
rendered whole once at about a million pixels.

What was on screen once comes back without a render. Tiles that leave the view
are kept and put back in the same frame when the view returns. A picture
already shown is never replaced by a second render of the same state; only the
exact render after editing replaces it.

### Zoom and scrolling

While nothing else waits, what a zoom about the pointer would show is rendered
for the wheel steps ahead (`PageLayoutEngine_Zoom.cpp`): up to twenty in and
ten out, one step in either direction for any point in view. `ZoomController`
applies a wheel or keyboard zoom one step at a time. A step waits at most
60 ms for its prepared picture, which then becomes the page's tiles or render
at that zoom without a copy; otherwise the step is shown at once and sharpened
when its render arrives. A zoom seen before comes back from the kept tiles or
from the page's earlier whole render. In edit mode only the nearest steps are
prepared.

A mouse wheel step glides instead of jumping (`SmoothScroll`), driven by the
window's frames; touchpad gestures are left as they are.

The grid of thumbnails and the find bar use the worker too: thumbnails in view
first, the search page by page with matches shown as they come in.

### Editing

While text is edited, only what differs from the unedited page is rendered
again: `PdfBackend::renderChanges()` compares the page objects before and after
the edits and renders that area, which is patched into the page
(`PageLayoutEngine_Edit.cpp`). PDFium rounds text by where a render starts, so
the last exact state of the area is rendered alongside and its pixels stay
wherever the change does not reach; the worker renders the page exactly once
editing pauses. Light pages are simply rendered whole. While text is edited no
zoom levels are prepared, so the worker stays free for the edit. After a zoom
the page being edited is rendered on the worker like any other; the area its
edits change is kept in points, so typing goes on patching at the new zoom.
`PdfiumBackend::editMetrics()` keeps its recent results, which the editor asks
for again on every zoom step.

## Annotations and content

Links and notes are read by `AnnotationLoader`: as much as fits 50 ms when the
document opens, the rest page by page on the worker. Any change to a link or
note first reads the remaining pages, so undo always sees the whole list.

`ContentProvider` (`engine/edit/ContentModel.hpp`) finds text lines,
paragraphs, table cells, form fields and images on a page. Hover only reads
its cache, because building a page synchronously stalls scrolling. When a
click finds no text, `EditController` falls back to OCR on a page rendered at
300 dpi or more.

## Changes and saving

The file on disk is only read until the user saves. Every change goes into the
journal: undo stack, change log, and snapshots in the session folder.

`DocumentView::saveToFile()` works in this order:

1. If the target is the file currently loaded, the view first moves onto a
   working copy (`detachSourceFrom()`), so PDFium never reads a file that is
   being replaced.
2. `stageDocument()` writes the backend's edits, then each overlay's
   `PageOverlay::writeTo()`, then changed bookmarks into a staging file. The
   edits are written by the worker while a local event loop keeps the
   spinner turning; user input waits until the file is written.
3. Pending digital signatures are signed into the staging file. A save that
   only signs copies the bytes unchanged, so signatures already in the file
   stay valid.
4. `SafeWrite::commit()` replaces the target. If that fails, the target is
   untouched.
5. The view reopens the saved file.

## Crash recovery

`SessionStore` (`app/`) keeps working copies, snapshots and a list of the open
documents. `SessionRecovery` (`ui/session/`) watches every view and writes a
recovery copy and a history archive for each one. The recovery copy is written
on the view's worker, so the window keeps working meanwhile. At the next start it offers
what an abandoned session left. `App` removes snapshots older than seven days
on startup.

## Import, export, OCR and signing

- **Import** (`engine/import/`): docx, odt and images become a PDF working
  copy before opening. The copy has no save target, so saving asks for a name.
- **Export** (`engine/export/`): PDF with page selection and password through
  qpdf, DOCX with layout detection, PNG per page, and printing.
- **OCR** (`engine/ocr/`): Tesseract, for text the page has no text layer for.
- **Signing** (`engine/sign/`, `ui/sign/`): a handwritten signature is placed
  as an image. A digital signature waits in `DigitalSignatureLayer` and is
  written on save by `PdfSignatureWriter` as an incremental update.
  Certificates come from PKCS#11 through p11-kit or PEM files on Linux, and
  from the certificate store through CNG on Windows. `SignatureManager::verify()`
  checks existing signatures; the interface does not call it yet.

## Modules

`modules/rich-media/` plugs in through three registers the Core names nothing
media-related in: `LeftSidebar::registerTool()`, `ToolPanels::add()` and
`PageOverlays::add()`, all filled from one static initializer in
`MediaModule.cpp`. A `PageOverlay` follows the document (`setDocument()`),
takes dropped files, writes itself on save (`writeTo()`), keeps its state for
history and recovery (`state()`, `restoreState()`) and reports changes to the
journal (`reportChange()`).

## Command line

`cli/` runs modes without a window on the same engine and views as the
application: `--export-pdf`, `--export-docx`, `--export-images`,
`--import-pdf`, `--organize-save`, `--select-text`, `--apply-edit` and the
`--shot-*` screenshots. They are the way to check a change without a display.

## Theme and language

`Theme::apply()` loads `Style.qss` or `Style.dark.qss`. Icons are SVGs tinted
at runtime by `Theme::renderSvg()` through nanosvg. Translations are the `.qm`
files in `resources/i18n/`; `MainWindow::applyLanguage()` swaps them at
runtime and calls `retranslateUi()` on the frame and every view.
