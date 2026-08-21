#!/usr/bin/env bash
# Cross-baut qpdf für Windows x86_64 (mingw-w64) und installiert es in einen
# projektlokalen Prefix, den build-win.sh danach findet.
#
# Fedora liefert kein mingw64-qpdf. Ohne dieses Skript ist im Windows-Build
# HAVE_QPDF nicht gesetzt, und damit fehlen dort zwei Funktionen komplett:
# der PDF-Export mit Optionen (pdfExportAvailable() ist false) und der
# vektorielle Save des Seiten-Organizers, der auf 150-dpi-Raster zurückfällt.
#
#   packaging/windows/build-qpdf-mingw.sh          # baut, wenn nicht vorhanden
#   packaging/windows/build-qpdf-mingw.sh --force  # baut in jedem Fall neu
#
# Die Version ist an die von Fedora gelieferte angeglichen, damit sich Linux-
# und Windows-Build nicht in der qpdf-Version unterscheiden.
set -euo pipefail

QPDF_VERSION="12.3.2"
QPDF_SHA256="6cba2f9f2cd887d905faeb99e0e51a307b217920d1bbf3e9cfbb2e8178a2deda"
QPDF_URL="https://github.com/qpdf/qpdf/releases/download/v${QPDF_VERSION}/qpdf-${QPDF_VERSION}.tar.gz"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${QPDF_MINGW_PREFIX:-$ROOT/third_party/mingw64}"
CACHE="$ROOT/third_party/.cache"
TOOLCHAIN="${TOOLCHAIN_FILE:-/usr/share/mingw/toolchain-mingw64.cmake}"
SYSROOT="/usr/x86_64-w64-mingw32/sys-root/mingw"
# qpdf sucht zlib und libjpeg über pkg-config. Das Host-pkg-config antwortet mit
# /usr/include und /usr/lib64 — also den Linux-Headern —, und der Build bricht
# dann in <cstdint> ab, weil MinGW uintptr_t anders definiert als glibc. Dieselbe
# Falle steht als Warnung im Root-CMakeLists.txt.
export PKG_CONFIG_LIBDIR="$SYSROOT/lib/pkgconfig:$SYSROOT/share/pkgconfig"
JOBS="${JOBS:-$(nproc)}"

FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

if [ "$FORCE" = 0 ] && [ -f "$PREFIX/lib/libqpdf.a" ]; then
    echo "==> qpdf liegt bereits in $PREFIX (--force baut neu)"
    exit 0
fi

[ -f "$TOOLCHAIN" ] || {
    echo "FEHLER: Toolchain-Datei nicht gefunden: $TOOLCHAIN" >&2
    echo "  Auf Fedora: sudo dnf install mingw64-filesystem" >&2
    exit 1
}

mkdir -p "$CACHE"
TARBALL="$CACHE/qpdf-${QPDF_VERSION}.tar.gz"

if [ ! -f "$TARBALL" ]; then
    echo "==> Lade qpdf ${QPDF_VERSION}…"
    curl -fsSL --retry 3 -o "$TARBALL.part" "$QPDF_URL"
    mv "$TARBALL.part" "$TARBALL"
fi

echo "==> Prüfe Prüfsumme…"
echo "${QPDF_SHA256}  ${TARBALL}" | sha256sum -c - >/dev/null || {
    echo "FEHLER: Prüfsumme stimmt nicht. Datei löschen und neu laden:" >&2
    echo "  rm $TARBALL" >&2
    exit 1
}

SRC="$CACHE/qpdf-${QPDF_VERSION}"
rm -rf "$SRC"
tar xzf "$TARBALL" -C "$CACHE"

BUILD="$CACHE/build-qpdf-mingw"
rm -rf "$BUILD"

echo "==> Konfiguriere…"
# Native Krypto statt OpenSSL: qpdf bringt alles mit, was für verschlüsselte
# PDFs nötig ist, und das spart eine DLL in der Auslieferung.
# Statische Libs aus: es wird nur die DLL ausgeliefert.
cmake -S "$SRC" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DPKG_CONFIG_EXECUTABLE=/usr/bin/x86_64-w64-mingw32-pkg-config \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_STATIC_LIBS=OFF \
    -DBUILD_DOC=OFF \
    -DUSE_IMPLICIT_CRYPTO=OFF \
    -DREQUIRE_CRYPTO_NATIVE=ON

# Nur die Bibliothek. Die Kommandozeilenwerkzeuge braucht OpenPDF Studio nicht,
# und sie sind es auch, die beim Cross-Build Ärger machen: qpdf sucht CRT_glob.o
# mit einem fest verdrahteten "gcc" (also dem Host-Compiler), und ein weiteres
# Ziel bündelt GCC-Runtime-DLLs aus einem Verzeichnis, das es hier nicht gibt.
echo "==> Baue libqpdf mit ${JOBS} Job(s)…"
cmake --build "$BUILD" --target libqpdf -j "$JOBS"

# Die install-Regel der lib-Komponente kopiert ein Verzeichnis, das nur der nicht
# gebaute CLI-Zweig füllt. Leer anlegen, sonst bricht die Installation dort ab.
mkdir -p "$BUILD/qpdf/extra-dlls"

echo "==> Installiere nach $PREFIX…"
cmake --install "$BUILD" --component lib
cmake --install "$BUILD" --component dev

# qpdf installiert die Bibliothek mit "TYPE LIBRARY". Auf Windows ist die DLL
# aber ein RUNTIME- und die Import-Bibliothek ein ARCHIVE-Artefakt, und beide
# fallen dadurch aus der Installation heraus — das exportierte CMake-Ziel zeigt
# anschließend auf zwei Dateien, die nicht da sind. Also von Hand nachlegen,
# genau dorthin, wo libqpdfTargets-release.cmake sie erwartet.
DLL="$(ls "$BUILD"/libqpdf/qpdf*.dll 2>/dev/null | head -1)"
IMPLIB="$(ls "$BUILD"/libqpdf/libqpdf.a 2>/dev/null | head -1)"
[ -n "$DLL" ]    || { echo "FEHLER: keine qpdf-DLL im Build gefunden" >&2; exit 1; }
[ -n "$IMPLIB" ] || { echo "FEHLER: keine Import-Bibliothek gefunden" >&2; exit 1; }
install -Dm755 "$DLL"    "$PREFIX/bin/$(basename "$DLL")"
install -Dm644 "$IMPLIB" "$PREFIX/lib/$(basename "$IMPLIB")"

echo "==> Fertig:"
ls -1 "$PREFIX"/bin/*.dll "$PREFIX"/lib/libqpdf* "$PREFIX"/lib/cmake/qpdf/qpdfConfig.cmake 2>/dev/null | sed 's/^/    /'
