# Licensing

[README](../../README.md) · [User documentation](../../README.md#user-documentation)

OpenPDF Studio has a dual-licensed Core and a separately licensed Rich Media
module. The [license overview](../../LICENSE), [licensing map](../../LICENSES/README.md)
and full license texts define the terms.

## Core

The viewer, editor, page organizer, annotations, importers, exporters and
application interface belong to the Core. It is available under
**GPL-3.0-only** or the **OpenPDF Studio Commercial License**.

The GPL option is free of charge and needs no separate agreement. The
commercial option requires a separate written agreement; publishing its
reference text does not itself grant commercial rights. The official
distribution uses the commercial option to combine the Core with the
Business-licensed module.

- [GPL-3.0 text](../../COPYING)
- [Commercial License](../../LICENSES/OPENPDF-COMMERCIAL.txt)

## Rich Media

`modules/rich-media/` is source-available under the
[OpenPDF Studio Business License](../../LICENSES/OPENPDF-BUSINESS.txt).
Personal use is free and requires no registration, activation or key.
Business use requires a Business License after a 30-day evaluation.

The evaluation period is defined per organization from its first business
use. The local reminder's installation date is an approximation and does not
change that term. License notices are reminders; a missing or invalid key does
not withhold application features.

## Usage declaration and keys

The Windows installer asks whether usage is personal or business. On other
packages, the application asks on first start; personal is preselected.
The application records the answer in `config.ini` as `[license] usage`, which
can be corrected there while the application is closed. It takes precedence
over the installer's declaration. Switching usage does not restart the local
evaluation period.

Business installations have a **License Key** page in Settings. Enter a valid
key there; a personal installation has no key page and no key prompt.
After the evaluation ends, a business installation without a valid key shows
a reminder at startup and continues to work.

Administrators can pass `/S /KEY=…` to the Windows installer for silent
deployment with a machine-wide key. A machine key takes precedence over a
per-user key. Technical storage and deployment details are in
[Rich media](../dev/rich-media.md#where-a-business-license-key-comes-from).

## Dependencies and earlier releases

Third-party components keep their own licenses. PDFium is the current PDF
engine and uses BSD-3-Clause with additional dependency notices; vendored
nanosvg and Monocypher have their own terms. See the
[licensing map](../../LICENSES/README.md) and [PDFium notices](../../LICENSES/PDFIUM.txt).

Releases published before 2026-08-17 were licensed under MIT; those earlier
grants remain valid. Contributions to the current Core follow its dual-license
terms as described in [Contributing](../../CONTRIBUTING.md#licensing).
