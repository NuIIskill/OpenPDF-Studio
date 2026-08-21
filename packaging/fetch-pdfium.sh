#!/usr/bin/env bash
# Holt die vorgebauten PDFium-Bibliotheken für Linux und Windows nach
# third_party/pdfium/ (gitignoriert).
#
# PDFium ist das, was auch in Qt6::Pdf steckt — hier ohne die Qt-Hülle und für
# beide Plattformen in derselben Version.
#
# Bewusst OHNE V8: Formular-JavaScript wird nicht gebraucht, und die Variante
# ohne V8 ist ein Vielfaches kleiner.
#
#   packaging/fetch-pdfium.sh           # holt, was fehlt
#   packaging/fetch-pdfium.sh --force   # lädt in jedem Fall neu
#
# Version und Prüfsummen sind gepinnt. Für ein Produkt, das ausgeliefert wird,
# ist ein fremdes Binary eine Lieferketten-Entscheidung: nicht "das neueste",
# sondern genau dieses, nachprüfbar. Beim Hochziehen der Version müssen die
# Prüfsummen mit — sonst bricht das Skript ab, und das ist so gewollt.
set -euo pipefail

PDFIUM_RELEASE="chromium/8009"     # PDFium 153.0.8009.0
PDFIUM_SHA256_LINUX="be513e8021a5bf8eb2116e00d78c3bacb82c5a02b3785156ae14fe5e33084385"
PDFIUM_SHA256_WIN="c78a8cd51b48abafcb266d868e401afefda1d189aa01ebbe743dc1d144e06031"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/third_party/pdfium"
CACHE="$ROOT/third_party/.cache"
BASE="https://github.com/bblanchon/pdfium-binaries/releases/download/${PDFIUM_RELEASE//\//%2F}"

FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

fetch_one() {
    local asset="$1" sha="$2" target="$3"

    if [ "$FORCE" = 0 ] && [ -f "$DEST/$target/PDFiumConfig.cmake" ]; then
        echo "==> $target liegt bereits vor"
        return 0
    fi

    local tarball="$CACHE/${asset}.tgz"
    if [ ! -f "$tarball" ]; then
        echo "==> Lade ${asset}…"
        curl -fsSL --retry 3 -o "$tarball.part" "${BASE}/${asset}.tgz"
        mv "$tarball.part" "$tarball"
    fi

    echo "${sha}  ${tarball}" | sha256sum -c - >/dev/null || {
        echo "FEHLER: Prüfsumme für ${asset} stimmt nicht." >&2
        echo "  Entweder wurde das Release ausgetauscht oder der Download ist kaputt." >&2
        echo "  Datei prüfen und löschen: rm $tarball" >&2
        exit 1
    }

    rm -rf "${DEST:?}/$target"
    mkdir -p "$DEST/$target"
    tar xzf "$tarball" -C "$DEST/$target"
    echo "    → $DEST/$target"
}

mkdir -p "$CACHE"
fetch_one pdfium-linux-x64 "$PDFIUM_SHA256_LINUX" linux-x64
fetch_one pdfium-win-x64   "$PDFIUM_SHA256_WIN"   win-x64

echo "==> PDFium $(sed -n 's/^MAJOR=//p' "$DEST/linux-x64/VERSION").$(sed -n 's/^BUILD=//p' "$DEST/linux-x64/VERSION") bereit"
echo "    Lizenztexte für die Auslieferung: $DEST/<plattform>/licenses/"
