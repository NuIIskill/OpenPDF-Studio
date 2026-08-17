# Licensing map

Which license covers which part of this repository, and what that means for a
build you make yourself.

## The two halves

| Path | License | SPDX |
| --- | --- | --- |
| everything not listed below — `src/`, `resources/`, `packaging/`, the build scripts | dual: GPL-3.0-only **or** OpenPDF Studio Commercial | `GPL-3.0-only OR LicenseRef-OpenPDF-Commercial` |
| `modules/rich-media/` | OpenPDF Studio Business (source-available) | `LicenseRef-OpenPDF-Business` |
| `src/3rdparty/nanosvg/` | zlib (vendored, upstream terms) | `Zlib` |

The Core is the whole application as it stands: viewer, renderer, editor, UI,
exporters, packaging. It is real free software under GPL-3.0-only — that path
costs nothing, needs no agreement, and is not crippled in any way.

The Commercial License is the alternative path the copyright holder uses for
the official distribution, because the GPL would not let that distribution
combine the Core with the Business-licensed module in one program. It grants
nothing on its own; see `OPENPDF-COMMERCIAL.txt`.

`modules/rich-media/` is the only source-available component. It is free for
personal use, and Business Use needs a Business License after a 30-day
evaluation; see `OPENPDF-BUSINESS.txt`. Its licensing notice is a reminder
only — dismissing it, or a broken check, changes nothing about the rights
granted (section 6 of that license).

## Files here

- `GPL-3.0.txt` — the GNU General Public License v3, verbatim.
- `OPENPDF-COMMERCIAL.txt` — the commercial option for the Core.
- `OPENPDF-BUSINESS.txt` — the license of the source-available components.

## Third-party dependencies

None of these is vendored except nanosvg; the rest are linked from the system.

| Component | License | Notes |
| --- | --- | --- |
| Qt 6 (Core, Widgets, Gui, Pdf, PrintSupport) | LGPL-3.0-or-later | dynamically linked, open-source edition |
| Poppler-Qt6 | GPL-2.0-or-later | optional alternative PDF backend — see the warning below |
| qpdf | Apache-2.0 | optional, vector PDF save and export options |
| Tesseract (+ Leptonica) | Apache-2.0 / BSD-2-Clause | optional, OCR |
| nanosvg | zlib | vendored in `src/3rdparty/nanosvg/` |

### The Poppler backend is GPL-only

`CMakeLists.txt` uses `Qt6::Pdf` when it is present and falls back to
Poppler-Qt6 otherwise. That fallback matters for licensing:

- A build linked against **Poppler** can only be distributed under the GPL. It
  must not be shipped under the Commercial License, and must not be combined
  with `modules/rich-media/` in a distributed binary.
- The **official distribution** — Commercial License plus Business-licensed
  module — must therefore be built with the `Qt6::Pdf` backend. Check the
  configure output: `PDF backend: Poppler-Qt6 (...)` means the resulting binary
  is GPL-only.

Everything else in the table above is GPL-compatible and also usable in a
proprietary distribution (Apache-2.0, BSD-2-Clause and zlib impose only notice
obligations; Qt is used under the LGPL through dynamic linking).

## Contributions

Contributions to the Core are accepted under the same dual license. Submitting
a contribution grants the copyright holder the right to distribute it under
both options — otherwise the dual-licensing model could not hold. Contributors
keep the copyright in their own work. See section 6 of
`OPENPDF-COMMERCIAL.txt`.

## Earlier releases

Releases published before 2026-08-17 were licensed under the MIT License. That
grant stays valid for those releases; it cannot be and is not withdrawn
retroactively. The change applies to the current source and to releases made
from here on.
