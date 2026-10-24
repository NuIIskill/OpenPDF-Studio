# Contributing to OpenPDF Studio

Bug reports, translations, UI improvements and focused pull requests are
welcome. For a larger change, open an
[issue](https://github.com/NuIIskill/OpenPDF-Studio/issues) first to discuss the approach.

## Get started

Follow [Building](docs/dev/building.md), then read the
[architecture](docs/dev/architecture.md) before changing document state,
rendering, saving or recovery. [Dependencies](docs/dev/dependencies.md) explains
which features are optional. [Testing](docs/dev/testing.md) lists command-line
checks and screenshot modes.

Keep each change focused on the requested behavior. Avoid introducing a
framework, migration or saved-state change for a one-off fix.

## Code layout and boundaries

| Directory | Responsibility |
| --- | --- |
| `src/cli/` | Command-line modes; only `main.cpp` includes the CLI |
| `src/app/` | Configuration, settings, passwords, sessions, safe writes and update checks |
| `src/engine/` | Document operations, editing, import, export, history, OCR, rendering and signing |
| `src/ui/` | Widgets, controllers, page layers and task-specific dialogs |
| `src/drm/` | Business-license state, key validation, notices and settings page |
| `src/3rdparty/` | Vendored nanosvg and Monocypher |
| `modules/rich-media/` | Separately licensed media implementation |

Follow these layer rules:

1. `engine/` never includes `ui/` or uses `QWidget` or `QDialog`.
2. `app/` never includes `engine/` or `ui/`; the engine may use app infrastructure.
3. Includes are root-relative to `src/`, for example
   `#include "ui/view/PageCanvas.hpp"`. Exceptions are AUTOMOC's `Foo.moc`
   and third-party headers.
4. UI folders name a task, such as `settings/` or `organizer/`. The frame
   (`bars/`, `panels/`, `theme/`), canvas (`view/`) and shared `widgets/`
   are exceptions. A widget with one owner belongs in that owner's task folder.
5. `src/` never includes module headers. Modules register with the Core;
   the Core must build and run when `modules/` is absent.

`drm/` intentionally holds both logic and its widget to keep the licensing
boundary together. Read [Rich media](docs/dev/rich-media.md) before changing
module integration.

Register a new source file in the `CMakeLists.txt` in its own folder with
`target_sources(OpenPDFStudio PRIVATE ...)`. A new folder also needs its own
`CMakeLists.txt` and an `add_subdirectory()` in its parent.

A `.cpp` above roughly 800 lines or a function above roughly 100 lines usually
needs responsibilities separated. New document-view state belongs in an
appropriate controller under `ui/view/`, not on `DocumentView` itself.

Write code comments in English. Class and namespace documentation comments
should be a short, single-sentence description. Keep implementation narration
out of method and field comments.

## Validate and document a change

- Build and run checks relevant to the changed behavior. Preserve compilation
  without optional dependencies and exercise a Core-only build for module work.
- Update [Architecture](docs/dev/architecture.md) when startup, rendering,
  saving, recovery or module flows change. Put user-facing instructions in
  `docs/user/` and technical details in `docs/dev/`; keep the README concise.
- Describe the problem, resulting behavior and validation in the pull request.

The [testing guide](docs/dev/testing.md#layer-boundary-checks) includes commands
for checking the layer boundaries.

## Bug reports and security

Include the version, operating system, package or build configuration,
reproduction steps, expected result and actual result. A small non-sensitive
document or screenshot helps. Do not attach private or sensitive PDFs to
public reports.

Report security issues privately to
[nullskilll@gmail.com](mailto:nullskilll@gmail.com), rather than opening a public issue.

## Licensing

Contributions to the Core are accepted under its dual license. By submitting
one, you grant the copyright holder the right to distribute it under both
GPL-3.0-only and the Commercial License while keeping copyright in your work.
The exact grant is in section 6 of the
[Commercial License](LICENSES/OPENPDF-COMMERCIAL.txt).

The Rich Media module follows the separate
[Business License](modules/rich-media/LICENSE). Files there carry
`SPDX-License-Identifier: LicenseRef-OpenPDF-Business`; do not move module code
into the Core. See the [licensing map](LICENSES/README.md) for path boundaries.
