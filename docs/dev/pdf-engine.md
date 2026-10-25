# PDF engine

[README](../../README.md) · [Developer documentation](../../README.md#developer-documentation)

PDFium is the only PDF engine on Linux and Windows. qpdf complements it for
PDF object access, preserving original content, export options and structural
changes. There are no Poppler or Qt6::Pdf backends in the current build.

## Obtaining PDFium

[packaging/fetch-pdfium.sh](../../packaging/fetch-pdfium.sh) fetches the same
pinned release for both platforms and verifies SHA-256 checksums before
extraction. The current script pins `chromium/8009` (PDFium 153.0.8009.0),
without V8. The application does not use form JavaScript.

The script writes `third_party/pdfium/linux-x64/` and `win-x64/`, including
`PDFiumConfig.cmake`. CMake uses those directories by default or accepts an
explicit `PDFium_DIR`. A version update must update the checksums as well.
Redistribution notices are collected in [LICENSES/PDFIUM.txt](../../LICENSES/PDFIUM.txt).

## Document boundary

| Type or file | Responsibility |
| --- | --- |
| `engine/document/PdfBackend.hpp` | Engine interface for opening, rendering, text, selection, images and saving |
| `PdfiumBackend` | The only implementation of that interface |
| `DocumentSource` | Owns a backend, renderer, content provider and worker for an open file |
| `engine/render/PdfRenderer` | Zoom and point-to-pixel conversion above the backend |
| `engine/edit/EditSession` | Pending changes, separate from the source PDF |
| `PdfiumEdits`, `PdfiumText`, `PdfiumFonts`, `PdfiumImages` | Apply text and image changes in PDFium's object model |
| `PdfiumWriter`, `OriginalContent`, `KeptText` | Write edited content while preserving original operators where possible |

These are engine types and never depend on widgets. See
[Architecture](architecture.md#backend-and-edits) for text layout, style
inheritance, clipboard source styles and original-content preservation.

## Coordinates

Above the backend, page coordinates are points measured from the top-left of
the displayed page, after CropBox and `/Rotate`. `PdfiumPageSpace` converts
between this space and PDF user space when reading or writing. CLI coordinates
use the same convention. CLI page numbers start at 1; backend page indices
start at 0.

## Rendering and concurrency

Every PDFium call holds `PdfiumLock`; the library is not used concurrently
within a process. `DocumentWorker` handles background operations and rendering.
Heavy documents can use `RenderPool` helper processes, each running the same
executable with `--render-server`, for unedited pages. Edited pages and document
operations stay on the document worker.

Helpers are stopped before replacing a file they have open. Memory and CPU
budgets, tiles, prepared zoom levels and incremental edit rendering are
described in [Architecture → Rendering](architecture.md#rendering).

## Editing and saving

The preview applies pending edits to the PDFium page object model.
`PdfiumWriter` applies the same edits for saving and generates page content.
PDFium regeneration can lose original constructs such as Type3 text, spacing,
soft masks and non-RGB colours. `OriginalContent` uses qpdf to preserve untouched
operators where it can; `KeptText` restores certain text operators on other paths.

![Inline text editor showing the original page text inside an editable frame with formatting controls above it](../images/screenshots/inline-text-editing.png)

*Use the inline preview as a visual reference when checking saved output:
glyph positions, line breaks and surrounding page content should agree.*

The view stages backend changes, overlay data, bookmarks, protection and
pending signatures before `SafeWrite::commit()` replaces the destination.
When saving over the loaded file, it first detaches onto a working copy.
Digital signatures use incremental updates, and a save without content changes
can copy the original bytes. See [Changes and saving](architecture.md#changes-and-saving).

qpdf handles operations PDFium does not cover, including rich-media
dictionaries, bookmark writing, encryption changes, PDF export rewriting and
signature updates. Keep those operations behind their feature definitions so
builds without optional libraries still compile.

Use the [testing commands](testing.md) to compare selection, edited previews,
saved pages and organizer output. Inspect saved results in another viewer when
checking preservation of PDF content.
