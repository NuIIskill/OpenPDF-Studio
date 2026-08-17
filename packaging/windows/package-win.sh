#!/usr/bin/env bash
# Package OpenPDF Studio for Windows:
#   1. dist/OpenPDF-Studio-<version>-win64-portable.zip   (portable, entpacken & starten)
#   2. dist/OpenPDF-Studio-<version>-Setup.exe            (Installer mit Uninstaller)
#
# Voraussetzungen:
#   - build-win/ mit fertigem Cross-Build (./build-win.sh) — wird sonst automatisch gebaut
#   - makensis:  natives Paket (dnf install mingw32-nsis)  ODER
#                NSIS-Windows-Distribution unter ~/.cache/openpdf-studio/nsis/makensis.exe (via Wine)
#                → wird bei Bedarf automatisch heruntergeladen
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-win}"
DIST_DIR="$ROOT/dist"
NSIS_VERSION="3.11"
NSIS_CACHE="${NSIS_CACHE:-$HOME/.cache/openpdf-studio/nsis-$NSIS_VERSION}"

VERSION="$(tr -d '[:space:]' < "$ROOT/version.txt")"
[[ -n "$VERSION" ]] || { echo "ERROR: Version nicht aus version.txt lesbar"; exit 1; }
echo "==> OpenPDF Studio $VERSION (win64)"

# ── 0. Build sicherstellen ────────────────────────────────────────────────────
if [[ ! -f "$BUILD_DIR/OpenPDFStudio.exe" ]]; then
    echo "==> Kein Windows-Build gefunden — starte ./build-win.sh"
    (cd "$ROOT" && ./build-win.sh)
fi

# ── 1. Staging: nur Laufzeitdateien, keine CMake-Artefakte ────────────────────
STAGE="$DIST_DIR/stage-win64"
APP_DIR="$STAGE/OpenPDF Studio"
rm -rf "$STAGE"
mkdir -p "$APP_DIR"

cp "$BUILD_DIR/OpenPDFStudio.exe" "$APP_DIR/"
cp "$BUILD_DIR"/*.dll "$APP_DIR/"
for d in platforms imageformats iconengines styles etc share; do
    [[ -d "$BUILD_DIR/$d" ]] && cp -r "$BUILD_DIR/$d" "$APP_DIR/"
done
# LICENSE ist nur die Übersicht und verweist auf LICENSES/ — beides mitgeben,
# sonst zeigt der Installer eine Lizenz, deren Volltexte fehlen.
cp "$ROOT/LICENSE" "$APP_DIR/"
cp -r "$ROOT/LICENSES" "$APP_DIR/"

SIZE="$(du -sh "$APP_DIR" | cut -f1)"
echo "==> Staging: $APP_DIR ($SIZE)"

# ── 2. Portable ZIP ───────────────────────────────────────────────────────────
PORTABLE_ZIP="$DIST_DIR/OpenPDF-Studio-$VERSION-win64-portable.zip"
rm -f "$PORTABLE_ZIP"
(cd "$STAGE" && zip -qr9 "$PORTABLE_ZIP" "OpenPDF Studio")
echo "==> Portable: $PORTABLE_ZIP ($(du -h "$PORTABLE_ZIP" | cut -f1))"

# ── 3. Setup.exe via NSIS ─────────────────────────────────────────────────────
SETUP_EXE="$DIST_DIR/OpenPDF-Studio-$VERSION-Setup.exe"
NSI="$ROOT/packaging/windows/installer.nsi"

run_makensis() {
    if command -v makensis >/dev/null 2>&1; then
        echo "==> makensis (nativ)"
        makensis -V2 \
            -DAPP_VERSION="$VERSION" \
            -DSTAGE_DIR="$APP_DIR" \
            -DOUT_FILE="$SETUP_EXE" \
            "$NSI"
        return
    fi

    # Fallback: Windows-NSIS unter Wine
    if [[ ! -f "$NSIS_CACHE/makensis.exe" ]]; then
        echo "==> Lade NSIS $NSIS_VERSION (Windows-Distribution) …"
        mkdir -p "$NSIS_CACHE"
        TMP_ZIP="$(mktemp --suffix=.zip)"
        curl -fL --retry 3 -o "$TMP_ZIP" \
            "https://downloads.sourceforge.net/project/nsis/NSIS%203/$NSIS_VERSION/nsis-$NSIS_VERSION.zip"
        unzip -q -o "$TMP_ZIP" -d "$NSIS_CACHE.tmp"
        mv "$NSIS_CACHE.tmp/nsis-$NSIS_VERSION"/* "$NSIS_CACHE/"
        rm -rf "$NSIS_CACHE.tmp" "$TMP_ZIP"
    fi

    command -v wine >/dev/null 2>&1 || {
        echo "ERROR: Weder makensis noch wine gefunden."
        echo "  Nativ:  sudo dnf install mingw32-nsis"
        exit 1
    }

    echo "==> makensis.exe unter Wine"
    WINEDEBUG=-all wine "$NSIS_CACHE/makensis.exe" -V2 \
        -DAPP_VERSION="$VERSION" \
        -DSTAGE_DIR="$(winepath -w "$APP_DIR")" \
        -DOUT_FILE="$(winepath -w "$SETUP_EXE")" \
        "$(winepath -w "$NSI")"
}

rm -f "$SETUP_EXE"
run_makensis
echo "==> Setup:    $SETUP_EXE ($(du -h "$SETUP_EXE" | cut -f1))"

echo
echo "Fertig — Artefakte in dist/:"
echo "  • $(basename "$PORTABLE_ZIP")  (entpacken, OpenPDFStudio.exe starten)"
echo "  • $(basename "$SETUP_EXE")  (Installer inkl. Uninstaller)"
