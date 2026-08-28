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
qpdf (`HAVE_QPDF`, PDF export options, the organizer's vector save and all
media reading and writing), Tesseract (`HAVE_TESSERACT`, OCR),
`Qt6::PrintSupport` (`HAVE_QT_PRINT`) and `modules/rich-media/`
(`HAVE_RICH_MEDIA`, which requires both qpdf and Qt Multimedia and is skipped
with a warning without them). Code behind these must still compile when they
are absent.

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
modules/
  rich-media/     media playback and embedding - separately licensed, see
                  its README; same engine//ui/ split as src/
.claude/testing/  unit tests and harnesses for the pure parts of engine/
```

Five rules keep this from eroding. They are cheap to check and were each
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

5. **`src/` never includes `modules/`.** The Core must build and run with
   `modules/` absent, and code that lives there is under a different license -
   a guarded `#include` in `src/` would quietly pull it back into the Core.
   The direction is inverted instead: the Core carries three small registers
   that name nothing media-related, and the module fills them from one static
   initializer in `modules/rich-media/MediaModule.cpp`.

   | register | what it takes | filled with |
   | --- | --- | --- |
   | `LeftSidebar::registerTool()` | a `ToolDef` | the *Rich Media* tool |
   | `ToolPanels::add()` | a panel bound to a tool id | the insert panel |
   | `PageOverlays::add()` | a `PageOverlay` factory | the media layer |

   `PageOverlay::writeTo()` is where an overlay writes its part into the
   staging file during a save. It sits on the overlay and not in a global list
   of passes on purpose: an overlay belongs to exactly one document view, so it
   needs no key to know which document is being saved - and a key would drift,
   because `DocumentView::detachSourceFrom()` swaps the content path in the
   middle of a save.

Check 5 with:

```bash
grep -rn '#include "rich-media/' src    # must be empty
```

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

## Scope discipline

Make the smallest change that directly satisfies the request. Do not turn a
one-off change into a generalized API, framework, migration, compatibility
layer or new behavior for saved user state unless the user explicitly asks for
it. If the request cannot be implemented without widening its scope, stop and
ask before changing anything.

## Comments

Write code comments in English. Documentation comments should only give a
short, one-sentence description of a class or namespace. Do not document
methods or fields, narrate the implementation or its history, or explain
behavior that is already clear from the code.

## Testing

**Nothing test-related is committed.** `tests/` is gitignored, and the root
`CMakeLists.txt` only builds it when it happens to be present locally. Test
fixtures, sample documents, harness scripts and their output belong in
`.claude/testing/` - also ignored. This sentence used to say the opposite
("fixtures are versioned under `tests/`, do not put them in ignored
directories"), which is how a set of generated fixtures nearly ended up in a
commit. If you think a fixture needs to be versioned, ask first.
