# Rich Media

Playback of media embedded in PDFs — `/Screen`, `/RichMedia` and `/Movie`
annotations — plus the UI for it.

```
SPDX-License-Identifier: LicenseRef-OpenPDF-Business
```

This module is **source-available, not open source**. It is free for personal
use; business use needs a Business License after a 30-day evaluation. The full
terms are in `LICENSE` next to this file (identical to
`LICENSES/OPENPDF-BUSINESS.txt`). Every other part of OpenPDF Studio is Core
and dual-licensed GPL-3.0-only or Commercial — see the repository `LICENSE`.

## Status

No code lives here yet. The directory exists so that the licensing boundary is
in place before the implementation lands, rather than being drawn around code
that already shipped as Core.

What exists today is Core, and stays Core — these are hooks, not the module:

- `src/engine/edit/ContentMap.cpp`, `src/engine/edit/ContentModel.cpp` —
  classify media annotations as `ContentItem::Type::Media` so the editor
  leaves them alone.
- `src/ui/view/HoverHighlight.cpp` — the red "Media" hover outline.
- `src/ui/panels/SettingsPanel.cpp` — the *Media Playback* settings page
  (system player / custom player). Currently a preference without a consumer.

Recognising a media annotation and refusing to edit it is viewer behaviour and
belongs to the Core. Playing it is the module.

## What belongs here

Anything whose only purpose is media playback: extracting the embedded stream,
the player surface drawn over the page, transport controls, the handoff to an
external player, and the module's own settings.

## What does not belong here

- Anything the Core needs in order to open, render, edit or save a document.
  If removing this directory breaks the viewer, the boundary was drawn wrong.
- Core types and helpers stay in `src/`; this module may include them, not the
  other way round. The Core must build and run with `modules/` absent.

## When code lands here

1. Give every file the SPDX header:
   `// SPDX-License-Identifier: LicenseRef-OpenPDF-Business`
2. Add `modules/rich-media/CMakeLists.txt` with its own
   `target_sources(OpenPDFStudio PRIVATE ...)`, as every source folder has,
   plus `add_subdirectory(modules/rich-media)` in the root `CMakeLists.txt` —
   guarded so a checkout without this directory still configures.
3. Keep it behind a `HAVE_RICH_MEDIA` define, so the Core-only build stays a
   build that actually gets exercised.
4. Remember the backend rule: a binary combining this module with the Core
   must be built under the Commercial License and therefore with the
   `Qt6::Pdf` backend, never with GPL-licensed Poppler. See
   `LICENSES/README.md`.
