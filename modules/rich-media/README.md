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

## Where a Business License key comes from

The key is deployment data, not a gate. Section 6 of the license is explicit:
the notice is a reminder, and the module must not stop working, withhold
features or make itself unusable when a check fails or is absent. Personal Use
never needs a key at all (section 2 — no registration, no activation).

The setup asks once which kind of use this is — one page, "Personal" preselected
— and records the answer as `HKLM\Software\OpenPDFStudio\Usage` =
`personal` | `business`. That answer is the first thing the module looks at: with
`personal` there is no notice, no countdown and no key prompt, ever. It decides
nothing else; both answers install and run the identical program.

Only the Windows setup can ask at install time. An RPM, a DEB or an AppImage
has nobody to ask, so the application does it itself on first start — same
question, same two answers, personal preselected — and stores it as
`[license] usage` in `config.ini`. That value also outranks the installer's,
because a declaration that cannot be corrected is worse than none. Correcting it
means editing that one line: it is deliberately not in the settings dialog,
where it would read as a switch that unlocks something.

The key itself is held in two places, machine before user:

| Scope | Windows | Set by |
| --- | --- | --- |
| machine | `HKLM\Software\OpenPDFStudio\BusinessLicense` — values `Key`, `Source`, `SetAt` | `Setup.exe /S /KEY=XXXX-XXXX`, i.e. an administrator rolling the product out |
| user | `config.ini`, `[license] businessKey` | the *License Key* settings page |

A machine key wins over a user key, so a site license covers every account on
the machine without anyone typing anything. `Usage` outranks both: a personal
declaration silences the reminder even if some key is present. The installer only writes the value
when `/KEY=` is passed, and never validates it — checking belongs to the
module, not to the setup. Uninstalling removes the machine key with the rest of
`HKLM\Software\OpenPDFStudio`.

### The standard install stays untouched

Nobody who is not doing business use may be confronted with any of this. The
setup asks nothing, shows no key field and writes no key unless an
administrator passes `/KEY=`; a normal installation is bit-identical to a
deployed one. In the module the same rule holds: no notice at start-up, none in
a personal-use installation, and none for anyone who never opens a document
with media in it. The reminder belongs at the point where the module is
actually used, once, dismissible — never as a gate and never as a greeting.

The 30-day Evaluation Period cannot be derived from either place: it runs per
organisation from the first business use (section 1.5), not per installation.
Whatever the module records locally is an approximation for the reminder, not
the term itself.

### What exists today

`src/drm/` is the placeholder for all of this: `LicenseStore` holds the state,
`LicensePage` is the settings page, `LicenseNotice` the two dialogs — the first
start's question and the expiry notice. Nothing there verifies anything: a
key is stored as typed, and the only thing actually computed is how many of the
30 days are left, counted from the first start of a business installation
(`license/evaluationStart`). Once they are up, the application shows the notice
at every start and otherwise carries on unchanged — the notice asks for a key,
it does not withhold anything, and no part of the program consults the result. The page is built only
where `Usage` is `business` — a personal installation has no License Key entry
in its settings at all. Switching between the two means editing
`[license] usage`, and the 30 days are not restarted by switching back and
forth — `[license] evaluationStart` is written once and never rewritten. `OPENPDF_USAGE=business|personal` overrides all of it
for tests and screenshots (`--shot-settings`, `--shot-license-notice`), which
report the state they find rather than declaring one.

Signed keys, a real check, and whatever the module does with the result belong
to the module and land with it.
