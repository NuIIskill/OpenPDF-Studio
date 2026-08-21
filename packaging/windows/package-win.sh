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
# sonst zeigt der Installer eine Lizenz, deren Volltexte fehlen. Im
# Installationsordner, nicht im Startmenü: Lizenztexte sind zum Nachschlagen da,
# nicht zum Anklicken.
cp "$ROOT/LICENSE" "$APP_DIR/LICENSE.txt"
cp -r "$ROOT/LICENSES" "$APP_DIR/"

# Bis August 2026 stand hier eine Fallunterscheidung: ein gegen Poppler
# gelinktes Binary durfte nur unter der GPL verteilt werden, ein Qt6::Pdf-Build
# auch kommerziell. Beide Backends sind durch PDFium ersetzt (BSD-3-Clause) —
# es gibt nur noch einen Build und nur noch eine Lizenzlage.
NSIS_LICENSE_FLAGS=()

# GPLv3 §6 verlangt den zugehörigen Quelltext. Für die Binärpakete genügt der
# Verweis auf das öffentliche Repository — samt Commit, damit "zugehörig" auch
# stimmt.
COMMIT="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo "unbekannt / unknown")"
{
    echo "OpenPDF Studio $VERSION (win64)"
    echo
    echo "Quelltext / source code:"
    echo "  https://github.com/NuIIskill/OpenPDF-Studio"
    echo "  commit $COMMIT"
    echo
    echo "Lizenz / license: GPL-3.0-only OR LicenseRef-OpenPDF-Commercial"
    echo "Siehe / see LICENSE.txt und LICENSES\\."
} > "$APP_DIR/SOURCE.txt"

# Portable-Modus ist eine Datei, keine Einstellung: liegt config.ini neben der
# .exe, speichert das Programm dort statt im Benutzerprofil. Nur ins ZIP — im
# Installationsordner wäre sie nicht beschreibbar und damit wirkungslos.
cat > "$APP_DIR/config.ini" <<'INI'
; OpenPDF Studio — portable Konfiguration / portable configuration
;
; Solange diese Datei neben OpenPDFStudio.exe liegt, speichert das Programm
; alle Einstellungen hier statt im Benutzerprofil.
; As long as this file sits next to OpenPDFStudio.exe, the program keeps all
; its settings here instead of in the user profile.
;
; Hinweis: das Programm schreibt die Datei beim Beenden neu — diese
; Kommentarzeilen verschwinden dabei.
INI

SIZE="$(du -sh "$APP_DIR" | cut -f1)"
echo "==> Staging: $APP_DIR ($SIZE)"

# ── 2. Portable ZIP ───────────────────────────────────────────────────────────
PORTABLE_ZIP="$DIST_DIR/OpenPDF-Studio-$VERSION-win64-portable.zip"
rm -f "$PORTABLE_ZIP"
(cd "$STAGE" && zip -qr9 "$PORTABLE_ZIP" "OpenPDF Studio")
echo "==> Portable: $PORTABLE_ZIP ($(du -h "$PORTABLE_ZIP" | cut -f1))"

# ── 3. Setup.exe via NSIS ─────────────────────────────────────────────────────
# Ab hier baut NSIS aus demselben Staging — ohne die portable config.ini.
rm -f "$APP_DIR/config.ini"

SETUP_EXE="$DIST_DIR/OpenPDF-Studio-$VERSION-Setup.exe"
NSI="$ROOT/packaging/windows/installer.nsi"

run_makensis() {
    if command -v makensis >/dev/null 2>&1; then
        echo "==> makensis (nativ)"
        makensis -V2 \
            ${NSIS_LICENSE_FLAGS[@]+"${NSIS_LICENSE_FLAGS[@]}"} \
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
        ${NSIS_LICENSE_FLAGS[@]+"${NSIS_LICENSE_FLAGS[@]}"} \
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
echo
echo "Stille Verteilung:  $(basename "$SETUP_EXE") /S [/KEY=XXXX-XXXX]"
echo "  /KEY legt den Business-Schlüssel maschinenweit ab (HKLM\\Software\\OpenPDFStudio\\BusinessLicense)."
