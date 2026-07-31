#!/usr/bin/env bash
# Package OpenPDF Studio für Linux-Distributionen:
#   1. dist/openpdf-studio-<version>-<arch>.rpm   (Fedora/RHEL/openSUSE)
#   2. dist/openpdf-studio_<version>_<arch>.deb   (Debian/Ubuntu)
#
# Voraussetzungen: rpmbuild (rpm-build), dpkg-deb (dpkg), cpack (cmake)
# Das AppImage baut separat: ./build.sh --appimage
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-pkg}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"
DIST_DIR="$ROOT/dist"

VERSION="$(tr -d '[:space:]' < "$ROOT/version.txt")"
[[ -n "$VERSION" ]] || { echo "ERROR: Version nicht aus version.txt lesbar"; exit 1; }
echo "==> OpenPDF Studio $VERSION (linux packages)"

# ── Welche Generatoren sind verfügbar? ───────────────────────────────────────
GENERATORS=()
command -v rpmbuild >/dev/null 2>&1 && GENERATORS+=("RPM") \
    || echo "==> WARN: rpmbuild fehlt — RPM wird übersprungen (dnf install rpm-build)"
command -v dpkg-deb >/dev/null 2>&1 && GENERATORS+=("DEB") \
    || echo "==> WARN: dpkg-deb fehlt — DEB wird übersprungen (dnf install dpkg)"
[[ ${#GENERATORS[@]} -gt 0 ]] || { echo "ERROR: weder rpmbuild noch dpkg-deb gefunden"; exit 1; }

# ── Build mit /usr-Prefix (separates Verzeichnis, stört build/ nicht) ────────
echo "==> Configuring ($BUILD_TYPE) → $BUILD_DIR/"
cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    "$@"

echo "==> Building mit $JOBS Job(s)…"
cmake --build "$BUILD_DIR" -j "$JOBS"

# ── Paketieren ───────────────────────────────────────────────────────────────
mkdir -p "$DIST_DIR"
IFS=';' GEN_LIST="${GENERATORS[*]}"; unset IFS
echo "==> cpack -G ${GEN_LIST}"
(cd "$BUILD_DIR" && cpack -G "$GEN_LIST" -B "$BUILD_DIR/packages")

shopt -s nullglob
for f in "$BUILD_DIR"/packages/*.rpm "$BUILD_DIR"/packages/*.deb; do
    mv -f "$f" "$DIST_DIR/"
    echo "==> $(basename "$f")  ($(du -h "$DIST_DIR/$(basename "$f")" | cut -f1))"
done
shopt -u nullglob

echo
echo "Fertig — Artefakte in dist/"
