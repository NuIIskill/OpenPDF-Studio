# Licensing map

Which license covers which part of this repository, and what that means for a
build you make yourself.

## The two halves

| Path | License | SPDX |
| --- | --- | --- |
| everything not listed below - `src/`, `resources/`, `packaging/`, the build scripts | dual: GPL-3.0-only **or** OpenPDF Studio Commercial | `GPL-3.0-only OR LicenseRef-OpenPDF-Commercial` |
| `modules/rich-media/` | OpenPDF Studio Business (source-available) | `LicenseRef-OpenPDF-Business` |
| `src/3rdparty/nanosvg/` | zlib (vendored, upstream terms) | `Zlib` |

The Core is the whole application as it stands: viewer, renderer, editor, UI,
exporters, packaging. It is real free software under GPL-3.0-only - that path
costs nothing, needs no agreement, and is not crippled in any way.

The Commercial License is the alternative path the copyright holder uses for
the official distribution, because the GPL would not let that distribution
combine the Core with the Business-licensed module in one program. It grants
nothing on its own; see `OPENPDF-COMMERCIAL.txt`.

`modules/rich-media/` is the only source-available component. It is free for
personal use, and Business Use needs a Business License after a 30-day
evaluation; see `OPENPDF-BUSINESS.txt`. Its licensing notice is a reminder
only - dismissing it, or a broken check, changes nothing about the rights
granted (section 6 of that license). Nothing in the module is gated on a key,
and that is deliberate: the sentence above would not survive a lock.

A build made from this repository contains the module unless it is turned off
with `-DOPENPDF_RICH_MEDIA=OFF` or the directory is absent. A distribution that
combines the two therefore ships under both licenses at once, which is what the
RPM `License:` field says.

## Files here

- `GPL-3.0.txt` - the GNU General Public License v3, verbatim.
- `OPENPDF-COMMERCIAL.txt` - the commercial option for the Core.
- `OPENPDF-BUSINESS.txt` - the license of the source-available components.

## Third-party dependencies

None of these is vendored except nanosvg; the rest are linked from the system.

| Component | License | Notes |
| --- | --- | --- |
| Qt 6 (Core, Widgets, Gui, PrintSupport) | LGPL-3.0-or-later | dynamically linked, open-source edition |
| PDFium (+ FreeType, libjpeg-turbo, lcms, OpenJPEG, libpng, zlib, ICU, AGG 2.3, abseil, simdutf, fast_float) | BSD-3-Clause und weitere permissive | die PDF-Engine; vollständige Sammlung in `PDFIUM.txt` |
| qpdf | Apache-2.0 | optional, vector PDF save and export options |
| Tesseract (+ Leptonica) | Apache-2.0 / BSD-2-Clause | optional, OCR |
| nanosvg | zlib | vendored in `src/3rdparty/nanosvg/` |


Everything else in the table above is GPL-compatible and also usable in a
proprietary distribution (Apache-2.0, BSD-2-Clause and zlib impose only notice
obligations; Qt is used under the LGPL through dynamic linking).

## Contributions

Contributions to the Core are accepted under the same dual license. Submitting
a contribution grants the copyright holder the right to distribute it under
both options - otherwise the dual-licensing model could not hold. Contributors
keep the copyright in their own work. See section 6 of
`OPENPDF-COMMERCIAL.txt`.

## Earlier releases

Releases published before 2026-08-17 were licensed under the MIT License. That
grant stays valid for those releases; it cannot be and is not withdrawn
retroactively. The change applies to the current source and to releases made
from here on.
