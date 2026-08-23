# OpenPDF Studio

Native Qt6 PDF viewer and editor: inline text editing, page organizer,
annotations, OCR, export to PDF/DOCX/images.

## Build

```bash
./build.sh                 # Release → build/OpenPDFStudio
cd build && ctest          # unit tests
```

PDFium is the PDF engine - the same build on Linux and Windows, fetched by
`packaging/fetch-pdfium.sh` (`HAVE_PDFIUM`, implies `HAVE_PDF_RENDERING`).
Without it the application starts but has no document functions at all.

The rest are optional and change what is compiled in, via `HAVE_*` defines:
qpdf (`HAVE_QPDF`, PDF export options and the organizer's vector save),
Tesseract (`HAVE_TESSERACT`, OCR), `Qt6::PrintSupport` (`HAVE_QT_PRINT`).
Code behind these must still compile when they are absent.

## Layout and layer rules

```
src/
  main.cpp        entry point
  app/            infrastructure: config.ini, settings, safe writes, session,
                  history, passwords
  drm/            business licence: state, settings page, the two notices
  engine/         document logic, no widgets
    document/     PdfBackend + the PDFium implementation: opening, rendering,
                  text lookup, selection, the content model and saving
    edit/         content model, ink metrics, exporters, session
    ocr/          Tesseract wrapper
    render/       PdfRenderer - zoom and point-to-pixel, asks the backend
  ui/             everything that is a widget
    bars/ panels/ tools/ theme/
    view/         controllers driving the document canvas
    edit/         the in-place text editor widgets
    settings/     the settings panel and the widgets only it uses
    organizer/    the page organizer and the widgets only it uses
    history/      the change-log timeline
    export/       the export dialog
    widgets/      shared widgets only - see the rule below
  3rdparty/       vendored (nanosvg)
tests/            unit tests for the pure parts of engine/
```

Four rules keep this from eroding. They are cheap to check and were each
broken once already:

1. **`engine/` never includes `ui/`, and never uses QWidget.** Anything that
   paints or takes input belongs in `ui/`. This is what makes the engine
   testable without a display.
2. **`app/` never includes `ui/`.** It is infrastructure that `ui/` builds on,
   not the other way round.
3. **Includes are always root-relative to `src/`**, e.g.
   `#include "ui/view/PageCanvas.hpp"` - never `"PageCanvas.hpp"` or
   `"view/PageCanvas.hpp"`. `src/` is on the include path. The only exceptions
   are AUTOMOC's own `"Foo.moc"` and `3rdparty/` headers.
4. **Every folder under `ui/` names a task, never a shape.** `settings/`,
   `organizer/`, `history/` and `export/` are each one thing the user does, with
   whatever widgets that takes. There is no `dialogs/` any more: it collected by
   form ("is a QDialog") and so had the organizer sitting outside it while being
   one - the same decay that had made `widgets/` mean "a widget that is not a
   bar, panel or dialog", where of nine files one was shared, six belonged to
   one owner each and two were used by nobody.

   `ui/widgets/` is the one exception, and only for widgets more than one owner
   uses - `IconButton`, `PasswordDialog`. A widget with a single owner lives in
   its task's folder, not here.

Check 1 and 2 with:

```bash
grep -rn '#include "ui/' src/engine src/app     # must be empty
grep -rln 'QWidget\|QDialog' src/engine         # must be empty
```

Rule 4 is checkable per file - count the owners outside its own folder:

```bash
grep -rl '\bIconButton\b' src --include=*.cpp | grep -v ui/widgets/ | wc -l
```

`drm/` is the one folder that holds both logic and its widget. The licensing
boundary is drawn around a directory, so everything that would have to move
together stays together - see `modules/rich-media/README.md`.

## Adding a file

Add it to the `CMakeLists.txt` **in its own folder** - each source directory
carries its own `target_sources(OpenPDFStudio PRIVATE ...)`. There is no
central source list. A new folder needs its own `CMakeLists.txt` plus an
`add_subdirectory()` line in its parent.

## Size limits

A `.cpp` over ~800 lines or a function over ~100 lines usually means several
responsibilities sharing a file. `DocumentView` is the standing example of what
that costs and is being split down accordingly - new state belongs in a
controller under `ui/view/`, not on the view.

## Testing

`tests/` covers the pure, widget-free parts (see `tst_inkmetrics.cpp` for the
pattern: paint a probe image, assert on the measurement). Anything needing a
document on screen is a manual pass.

**Nothing test-related is committed.** `tests/` is gitignored, and the root
`CMakeLists.txt` only builds it when it happens to be present locally. Test
fixtures, sample documents, harness scripts and their output belong in
`.claude/testing/` - also ignored. This sentence used to say the opposite
("fixtures are versioned under `tests/`, do not put them in ignored
directories"), which is how a set of generated fixtures nearly ended up in a
commit. If you think a fixture needs to be versioned, ask first.
