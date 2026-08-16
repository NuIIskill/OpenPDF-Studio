# OpenPDF Studio

Native Qt6 PDF viewer and editor: inline text editing, page organizer,
annotations, OCR, export to PDF/DOCX/images.

## Build

```bash
./build.sh                 # Release → build/OpenPDFStudio
cd build && ctest          # unit tests
```

Optional dependencies change what is compiled in, via `HAVE_*` defines:
`Qt6::Pdf` or Poppler-Qt6 (`HAVE_PDF_RENDERING`), qpdf (`HAVE_QPDF`, vector
save), Tesseract (`HAVE_TESSERACT`, OCR), `Qt6::PrintSupport` (`HAVE_QT_PRINT`).
Code behind these must still compile when they are absent.

## Layout and layer rules

```
src/
  main.cpp        entry point
  app/            infrastructure: settings, safe writes, session, history, passwords
  engine/         document logic, no widgets
    edit/         content model, text extraction, ink metrics, exporters, session
    ocr/          Tesseract wrapper
    render/       PdfRenderer — backend-neutral page rasterisation
  ui/             everything that is a widget
    bars/ panels/ dialogs/ tools/ widgets/ theme/
    view/         controllers driving the document canvas
    edit/         the in-place text editor widgets
  3rdparty/       vendored (nanosvg)
tests/            unit tests for the pure parts of engine/
```

Three rules keep this from eroding. They are cheap to check and were each
broken once already:

1. **`engine/` never includes `ui/`, and never uses QWidget.** Anything that
   paints or takes input belongs in `ui/`. This is what makes the engine
   testable without a display.
2. **`app/` never includes `ui/`.** It is infrastructure that `ui/` builds on,
   not the other way round.
3. **Includes are always root-relative to `src/`**, e.g.
   `#include "ui/view/PageCanvas.hpp"` — never `"PageCanvas.hpp"` or
   `"view/PageCanvas.hpp"`. `src/` is on the include path. The only exceptions
   are AUTOMOC's own `"Foo.moc"` and `3rdparty/` headers.

Check 1 and 2 with:

```bash
grep -rn '#include "ui/' src/engine src/app     # must be empty
grep -rln 'QWidget\|QDialog' src/engine         # must be empty
```

## Adding a file

Add it to the `CMakeLists.txt` **in its own folder** — each source directory
carries its own `target_sources(OpenPDFStudio PRIVATE ...)`. There is no
central source list. A new folder needs its own `CMakeLists.txt` plus an
`add_subdirectory()` line in its parent.

## Size limits

A `.cpp` over ~800 lines or a function over ~100 lines usually means several
responsibilities sharing a file. `DocumentView` is the standing example of what
that costs and is being split down accordingly — new state belongs in a
controller under `ui/view/`, not on the view.

## Testing

`tests/` covers the pure, widget-free parts (see `tst_inkmetrics.cpp` for the
pattern: paint a probe image, assert on the measurement). Anything needing a
document on screen is a manual pass. Test fixtures are versioned under
`tests/`; do not put them in ignored directories.
