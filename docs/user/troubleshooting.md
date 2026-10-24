# Troubleshooting

[README](../../README.md) · [User documentation](../../README.md#user-documentation)

Start with the application version, operating system, package type and exact
error. Version 0.3.2 is an alpha and saving replaces the original file, so
reproduce an editing or saving problem on a backup. For source builds, consult [Dependencies](../dev/dependencies.md).

## No PDF opens or a feature is missing

PDFium is the only PDF backend. A source build can start without it but cannot
open or process documents. Run `packaging/fetch-pdfium.sh` and rebuild; check
the configure output for the detected PDFium backend.

| Missing feature | Check |
| --- | --- |
| PDF export controls, vector organizer save, bookmarks or protection | qpdf was found during configuration |
| Digital signing | qpdf and the platform signing dependencies; GnuTLS and p11-kit on Linux |
| Rich Media tool | Module enabled and present, qpdf available, plus Qt Multimedia on Linux |
| Editing a scan | Tesseract compiled in and OCR language data available |
| Opening `.docx` or `.odt` | zlib included in the build |
| TIFF or WebP import | Qt image format plugins installed or packaged |
| Printing | Qt PrintSupport included and an operating-system printer configured |

If a tool was hidden, use **Customize Tools** to show it again.
PDF/A export, redaction, a complete form editor, signing timestamps and locking
after signing are not implemented.

## Linux startup and Qt platform errors

The Linux application defaults to Wayland unless `QT_QPA_PLATFORM` is set.
Check that a Wayland session and the Wayland Qt plugin are available. The
AppImage removes X11/XCB support during packaging, so it needs Wayland.
A native build may use another Qt platform if that platform's plugins are installed.

For a source build, launch from a terminal to capture startup errors. An
AppImage that crashes at startup may need to be rebuilt with the packaging
script and system `patchelf` 0.18 or later; see [Releasing](../dev/releasing.md).

## Windows startup

Keep the executable, DLLs and plugin folders together. Re-extract the complete
portable ZIP if files were moved individually. Media Feature Pack components
can be absent on Windows N; the application should still start, though media
playback or conversion can be unavailable.

## Text selection, OCR and layout

If a click chooses the wrong paragraph, use Shift+drag to select the intended
characters. Scanned text needs OCR; check that English, German or French
Tesseract data is installed. The Windows package reads it from `share/tessdata`
next to the executable. OCR errors and approximate font matching need review.

For imported Word or OpenDocument files, compare the result with the original.
Headers, footers, fields, footnotes, columns and tracked changes are not
supported. See [Editing](editing.md) for details.

## Digital signatures and certificates

If digital signing is unavailable, check the build dependencies first. On
Linux, ensure the token's provider is registered with p11-kit; on Windows,
ensure the certificate is available in the certificate store. A self-signed
certificate can be created in the dialog for local testing.

An untrusted signer does not necessarily mean the file was changed. Review the
verification result and certificate identity before adding local trust. After
editing signed content, verify the saved file again. See [Signatures](signatures.md).

## Video does not play

The module plays only embedded assets and asks before first playback of
existing media in each document. A reference to an external file or URL is
refused. Presentation mode currently displays only the poster.

For a decoder failure, use the offered 1080p playback copy or explicitly
choose an external player. In-app playback settings do not silently fall back
to an external process. H.264 in MP4 is the intended format; Linux conversion
needs ffmpeg and playback needs a working Qt Multimedia backend. On Windows,
system encoders are used first with ffmpeg as fallback. Copy the diagnostic
details from a playback error when reporting it.

## Saving and exporting

Check the destination folder's permissions, available disk space and whether
another program has locked the file. Try **Save As** to a writable folder.
Saving stages the PDF and atomically replaces the destination only after
writing succeeds. See [Export](export.md) for page ranges, output options and
format limitations.

After a crash, accept the recovery offered on the next start and inspect the
recovered document before saving. Enter opening passwords again as needed.
Pending protection passwords are not persisted; reapply protection before saving.

## Settings and reports

Settings use a writable `config.ini` beside the executable when present;
otherwise they use the platform's generic configuration directory under
`OpenPDF/OpenPDFStudio/config.ini`. On Linux this is normally
`~/.config/OpenPDF/OpenPDFStudio/config.ini`. `OPENPDF_CONFIG` overrides the
location. Recovery files use Qt's application data directory, in `session/`,
independently of portable settings.

Report reproducible bugs through [GitHub Issues](https://github.com/NuIIskill/OpenPDF-Studio/issues)
with the version, package, steps, expected result and actual result. Attach
only a non-sensitive example you are allowed to share. Report security issues
privately to [nullskilll@gmail.com](mailto:nullskilll@gmail.com).
