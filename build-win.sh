#!/usr/bin/env bash
# Cross-compile OpenPDF Studio for Windows x86_64
# Uses the mingw-w64 + Qt6-for-Windows toolchain installed on this system.
#
# Install dependencies (Fedora):
#   sudo dnf install mingw64-qt6-qtbase-devel mingw64-qt6-qtpdf
#   (or equivalent for your distro)
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build-win}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"

# ── Toolchain detection (in priority order) ───────────────────────────────────
TOOLCHAIN_FILE="${TOOLCHAIN_FILE:-}"

if [[ -z "$TOOLCHAIN_FILE" ]]; then
    # 1. System mingw64 Qt6 toolchain (Fedora mingw64-qt6-* packages)
    CANDIDATES=(
        "/usr/x86_64-w64-mingw32/sys-root/mingw/lib/cmake/Qt6/qt.toolchain.cmake"
        "/usr/share/mingw/toolchain-mingw64.cmake"
    )
    for f in "${CANDIDATES[@]}"; do
        if [[ -f "$f" ]]; then
            TOOLCHAIN_FILE="$f"
            echo "==> Using toolchain: $f"
            break
        fi
    done
fi

if [[ -z "$TOOLCHAIN_FILE" ]]; then
    echo "ERROR: No Windows cross-compile toolchain found."
    echo "  On Fedora:  sudo dnf install mingw64-qt6-qtbase-devel"
    echo "  On Ubuntu:  sudo apt install qt6-base-dev (then set TOOLCHAIN_FILE=)"
    echo "  Custom:     TOOLCHAIN_FILE=/path/to/toolchain.cmake ./build-win.sh"
    exit 1
fi

echo "==> Configuring (${BUILD_TYPE}) → ${BUILD_DIR}/"
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    "${@}"

echo "==> Building with ${JOBS} job(s)…"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

# ── Deploy: copy runtime DLLs so the .exe is self-contained ──────────────────
QT_BIN="/usr/x86_64-w64-mingw32/sys-root/mingw/bin"
PLUGIN_DIR="/usr/x86_64-w64-mingw32/sys-root/mingw/lib/qt6/plugins"

if [[ -d "$QT_BIN" ]]; then
    echo "==> Deploying runtime DLLs → ${BUILD_DIR}/"

    # MinGW runtime
    for dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
        [[ -f "${QT_BIN}/${dll}" ]] && cp -u "${QT_BIN}/${dll}" "${BUILD_DIR}/"
    done

    # Qt6 core DLLs
    for dll in Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6PrintSupport.dll Qt6Svg.dll Qt6SvgWidgets.dll Qt6Pdf.dll; do
        [[ -f "${QT_BIN}/${dll}" ]] && cp -u "${QT_BIN}/${dll}" "${BUILD_DIR}/"
    done

    # Poppler PDF backend (used when Qt6Pdf is unavailable)
    for dll in libpoppler-qt6-3.dll libpoppler-156.dll liblcms2-2.dll libjpeg-62.dll; do
        [[ -f "${QT_BIN}/${dll}" ]] && cp -u "${QT_BIN}/${dll}" "${BUILD_DIR}/"
    done

    # Qt6Core transitive deps: ICU + PCRE2 + zlib
    for dll in icui18n77.dll icuuc77.dll icudata77.dll libpcre2-16-0.dll zlib1.dll; do
        [[ -f "${QT_BIN}/${dll}" ]] && cp -u "${QT_BIN}/${dll}" "${BUILD_DIR}/"
    done

    # Qt6Gui transitive deps: font stack
    for dll in libfontconfig-1.dll libfreetype-6.dll libharfbuzz-0.dll libpng16-16.dll \
               libexpat-1.dll libbz2-1.dll libglib-2.0-0.dll libintl-8.dll libpcre2-8-0.dll \
               iconv.dll; do
        [[ -f "${QT_BIN}/${dll}" ]] && cp -u "${QT_BIN}/${dll}" "${BUILD_DIR}/"
    done

    # Platform plugin
    if [[ -d "${PLUGIN_DIR}/platforms" ]]; then
        mkdir -p "${BUILD_DIR}/platforms"
        cp -u "${PLUGIN_DIR}/platforms/qwindows.dll" "${BUILD_DIR}/platforms/" 2>/dev/null || true
    fi

    # Image format plugins (JPEG, PNG, SVG, GIF, ICO)
    if [[ -d "${PLUGIN_DIR}/imageformats" ]]; then
        mkdir -p "${BUILD_DIR}/imageformats"
        cp -u "${PLUGIN_DIR}/imageformats/"*.dll "${BUILD_DIR}/imageformats/" 2>/dev/null || true
    fi

    # Icon engine for SVG icons
    if [[ -d "${PLUGIN_DIR}/iconengines" ]]; then
        mkdir -p "${BUILD_DIR}/iconengines"
        cp -u "${PLUGIN_DIR}/iconengines/"*.dll "${BUILD_DIR}/iconengines/" 2>/dev/null || true
    fi

    # Windows style plugin
    if [[ -d "${PLUGIN_DIR}/styles" ]]; then
        mkdir -p "${BUILD_DIR}/styles"
        cp -u "${PLUGIN_DIR}/styles/"*.dll "${BUILD_DIR}/styles/" 2>/dev/null || true
    fi

    echo "==> Deploy complete."
else
    echo "==> WARN: Qt bin dir not found at ${QT_BIN} — skipping DLL deploy."
fi

echo "==> Done: ${BUILD_DIR}/OpenPDFStudio.exe"
