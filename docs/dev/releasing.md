# Releasing

[README](../../README.md) · [Developer documentation](../../README.md#developer-documentation)

Run packaging from the repository root. The scripts create artifacts in
`dist/`; they do not publish a GitHub release. Build and deployment details are
in [Building](building.md) and [Dependencies](dependencies.md).

## Version and documentation

`version.txt` is the central version source. CMake reads it for `APP_VERSION`
and package metadata, and the packaging scripts use it in filenames.

Before packaging, set the intended version and update the README's version and
installation examples, [Installation](../user/installation.md), and any changed
feature documentation. Keep
[Architecture](architecture.md) aligned when document flows change.

The update checker compares repository tags. A published release tag must
identify the intended source revision and version. Build distributable artifacts
from that revision so their source reference is accurate.

## Translations

Precompiled `.qm` files are embedded through `resources/openpdf-studio.qrc`.
The native build can update the `.ts` sources when Qt LinguistTools is installed:

```bash
cmake --build build --target update_translations
```

Review and translate changed entries in `resources/i18n/`, regenerate the
corresponding `.qm` files with Qt's `lrelease`, and rebuild so the resources
include them. Updating `.ts` files alone does not update the shipped translations.
Check affected screens in both themes and in languages with longer labels.

## Linux RPM and DEB

Fetch PDFium and run the package wrapper:

```bash
packaging/fetch-pdfium.sh
packaging/linux/package-linux.sh
```

The wrapper uses a separate `build-pkg/` directory, Release and `/usr` by
default. It accepts `BUILD_DIR`, `BUILD_TYPE`, `JOBS` and additional CMake
arguments. It invokes CPack for available generators: `rpmbuild` is needed for
RPM, `dpkg-deb` for DEB, and at least one must be present. Use the wrapper so
the prefix, output location and artifact collection are applied consistently.

PDFium is installed in `lib/openpdf-studio` and found through the executable's
install RPATH. RPM dependencies are derived from linked libraries, with
PDFium excluded because it is shipped in the package. DEB dependencies are
maintained explicitly in the root CMake file; review them when changing linked
libraries. Qt image plugins and multimedia backends also need runtime coverage.

## Linux AppImage

```bash
./build.sh --appimage
```

This builds a Release application and stages it in `build/AppDir/` by default,
then writes `dist/OpenPDF_Studio-<version>-x86_64.AppImage`. The script locates
or downloads linuxdeploy, its Qt plugin, appimagetool and a cached AppImage
runtime. `TOOLS_DIR` defaults to `~/.local/bin`; `ARCH` defaults to `x86_64`.
The fetched PDFium binaries target x86_64, so setting `ARCH` alone does not
produce a supported build for another architecture.

The script preloads Wayland and multimedia plugins, deploys libraries and
repatches their RPATHs with system `patchelf` when available. Use `patchelf`
0.18 or later to avoid the bundled older patchelf's startup corruption.
The packaging removes X11/XCB files, bundled libffi and generated `qt.conf`,
then writes an `AppRun` using the bundled library and plugin directories.
The resulting AppImage requires a Wayland session and host Wayland libraries.

Test file dialogs and media playback in the packaged image as well as startup;
these exercise libraries and plugins loaded only at runtime.

## Windows portable ZIP and installer

```bash
packaging/fetch-pdfium.sh
packaging/windows/build-qpdf-mingw.sh
./build-win.sh
packaging/windows/package-win.sh
```

The packaging script uses `build-win/` by default and builds if the executable
is absent. Rebuild explicitly after source or resource changes; an existing
executable is not refreshed by packaging. It stages the executable, DLLs,
plugins, font configuration, OCR data and licenses, then creates:

- `dist/OpenPDF-Studio-<version>-win64-portable.zip`
- `dist/OpenPDF-Studio-<version>-Setup.exe`

ZIP creation needs `zip`. The installer uses native `makensis` when available;
otherwise the script downloads the pinned Windows NSIS distribution and runs
it through Wine. `NSIS_CACHE` can override its cache directory.

The portable ZIP carries a writable `config.ini` next to the executable.
That file is removed from staging before the installer is created so an
installed application uses per-user settings. `SOURCE.txt` in the package
records the repository and current commit.

For administrator deployment, the installer accepts `/S` and an optional
`/KEY=…` machine-wide Business License key. See
[Rich media](rich-media.md#where-a-business-license-key-comes-from) for storage
and precedence.

## Validate artifacts and publish

Use the relevant checks in [Testing](testing.md), then install or extract each
package in a clean target environment. Check PDF opening, editing, saved output,
export, OCR, signing and printing as included, plus media playback and conversion
for a full build. Check settings persistence and the portable folder's behavior.
Confirm optional runtime plugins, language data and license texts are present.

Include the [license overview](../../LICENSE), the full `LICENSES/` directory
and the notices for shipped dependencies. The official distribution combines
the Core's commercial option with the Business-licensed module; see the
[licensing map](../../LICENSES/README.md).

Publish the verified artifacts and release notes for the intended tag on
[GitHub Releases](https://github.com/NuIIskill/OpenPDF-Studio/releases), and
keep the download page aligned. Note known limitations and the alpha status.
