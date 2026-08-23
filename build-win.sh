#!/usr/bin/env bash
# Cross-compile OpenPDF Studio for Windows x86_64
# Uses the mingw-w64 + Qt6-for-Windows toolchain installed on this system.
#
# Install dependencies (Fedora):
#   sudo dnf install mingw64-filesystem mingw64-qt6-qtbase mingw64-qt6-qtsvg
#
# Zwei Abhängigkeiten kommen NICHT aus den Fedora-Paketen und müssen einmalig
# beschafft werden — ohne sie fehlen ganze Funktionen:
#
#   packaging/fetch-pdfium.sh                  PDF-Anzeige, -Text und -Speichern
#   packaging/windows/build-qpdf-mingw.sh      PDF-Export mit Optionen, Organizer
#
# Poppler und Qt6::Pdf werden nicht mehr gebraucht: beide Plattformen laufen
# auf PDFium.
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

# ── Cross-gebaute Abhängigkeiten ─────────────────────────────────────────────
# Fedora liefert kein mingw64-qpdf. Ohne qpdf fehlen im Windows-Build der
# PDF-Export mit Optionen und der vektorielle Save des Organizers — beides
# ersatzlos, nicht bloß eingeschränkt.
QPDF_PREFIX="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/third_party/mingw64"
MINGW_SYSROOT="${MINGW_SYSROOT:-/usr/x86_64-w64-mingw32/sys-root/mingw}"
if [[ -f "${QPDF_PREFIX}/lib/cmake/qpdf/qpdfConfig.cmake" ]]; then
    echo "==> qpdf: ${QPDF_PREFIX}"
    # Weder CMAKE_PREFIX_PATH noch CMAKE_FIND_ROOT_PATH helfen hier:
    # die mingw-Toolchain setzt CMAKE_FIND_ROOT_PATH_MODE_PACKAGE auf ONLY —
    # damit sucht find_package() nur noch unterhalb von CMAKE_FIND_ROOT_PATH —
    # und überschreibt diesen Pfad anschließend mit einem schlichten SET(),
    # das auch ein -D von der Kommandozeile aussticht. Ein direkt gesetztes
    # <Paket>_DIR wird dagegen unverändert verwendet.
    PREFIX_ARGS=(-Dqpdf_DIR="${QPDF_PREFIX}/lib/cmake/qpdf")
else
    echo "==> qpdf: nicht gebaut — PDF-Export und Organizer-Vektorsave fehlen."
    echo "    Bauen mit: packaging/windows/build-qpdf-mingw.sh"
    PREFIX_ARGS=()
fi

echo "==> Configuring (${BUILD_TYPE}) → ${BUILD_DIR}/"
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    "${PREFIX_ARGS[@]:+${PREFIX_ARGS[@]}}" \
    "${@}"

echo "==> Building with ${JOBS} job(s)…"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

# ── Deploy: copy runtime DLLs so the .exe is self-contained ──────────────────
QT_BIN="/usr/x86_64-w64-mingw32/sys-root/mingw/bin"
PLUGIN_DIR="/usr/x86_64-w64-mingw32/sys-root/mingw/lib/qt6/plugins"

if [[ -d "$QT_BIN" ]]; then
    echo "==> Deploying runtime DLLs → ${BUILD_DIR}/"

    # All runtime DLLs (full recursive dep tree of OpenPDFStudio.exe)
    for dll in \
        libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll \
        Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6PrintSupport.dll Qt6Svg.dll Qt6SvgWidgets.dll \
        Qt6Network.dll libcrypto-3-x64.dll \
        icui18n77.dll icuuc77.dll icudata77.dll libpcre2-16-0.dll zlib1.dll \
        libfontconfig-1.dll libfreetype-6.dll libharfbuzz-0.dll libpng16-16.dll \
        libexpat-1.dll libbz2-1.dll libglib-2.0-0.dll libintl-8.dll libpcre2-8-0.dll \
        iconv.dll; do
        [[ -f "${QT_BIN}/${dll}" ]] && cp -u "${QT_BIN}/${dll}" "${BUILD_DIR}/"
    done

    # PDFium kommt als vorgebautes Binary (packaging/fetch-pdfium.sh) und liegt
    # ebenfalls außerhalb des Sysroots.
    PDFIUM_BIN="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/third_party/pdfium/win-x64/bin"
    if [[ -f "${PDFIUM_BIN}/pdfium.dll" ]]; then
        cp -u "${PDFIUM_BIN}/pdfium.dll" "${BUILD_DIR}/"
        echo "  pdfium: pdfium.dll deployed"
    fi

    # qpdf liegt nicht im Qt-Sysroot, sondern im projektlokalen Prefix.
    if [[ -d "${QPDF_PREFIX}/bin" ]]; then
        cp -u "${QPDF_PREFIX}"/bin/qpdf*.dll "${BUILD_DIR}/" 2>/dev/null && \
            echo "  qpdf: $(basename "$(ls "${QPDF_PREFIX}"/bin/qpdf*.dll | head -1)") deployed"
    fi

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

    # TLS-Backend — ohne das Plugin scheitert jedes https, und die
    # Update-Prüfung ist die einzige Verbindung, die das Programm aufbaut.
    # Nur Schannel: das nimmt Windows' eigenen Zertifikatsspeicher und braucht
    # keine mitgelieferte CA-Liste. Das OpenSSL-Backend bliebe ohne
    # libssl-3-x64.dll ohnehin ungeladen. (libcrypto-3-x64.dll oben ist davon
    # unabhängig — Qt6Network.dll importiert sie fest, ohne sie startet die
    # .exe gar nicht.)
    if [[ -d "${PLUGIN_DIR}/tls" ]]; then
        mkdir -p "${BUILD_DIR}/tls"
        cp -u "${PLUGIN_DIR}/tls/qschannelbackend.dll" "${BUILD_DIR}/tls/" 2>/dev/null || true
    fi

    # Meldet, ob überhaupt ein Netz da ist.
    if [[ -d "${PLUGIN_DIR}/networkinformation" ]]; then
        mkdir -p "${BUILD_DIR}/networkinformation"
        cp -u "${PLUGIN_DIR}/networkinformation/"*.dll "${BUILD_DIR}/networkinformation/" 2>/dev/null || true
    fi

    # ── fontconfig config (fixes fontconfig init crash on Windows) ──────────
    # The cross-compiled libfontconfig-1.dll has Linux paths hardcoded; deploy
    # the proper Windows-aware fonts.conf so it can find Windows system fonts.
    FC_ETC="/usr/x86_64-w64-mingw32/sys-root/mingw/etc/fonts"
    FC_SHARE="/usr/x86_64-w64-mingw32/sys-root/mingw/share/fontconfig"
    if [[ -f "${FC_ETC}/fonts.conf" ]]; then
        mkdir -p "${BUILD_DIR}/etc/fonts/conf.d"
        cp -u "${FC_ETC}/fonts.conf" "${BUILD_DIR}/etc/fonts/"
        [[ -d "${FC_ETC}/conf.d" ]] && cp -ru "${FC_ETC}/conf.d/." "${BUILD_DIR}/etc/fonts/conf.d/"
        echo "  fontconfig: fonts.conf deployed"
    fi
    if [[ -d "${FC_SHARE}/conf.avail" ]]; then
        mkdir -p "${BUILD_DIR}/share/fontconfig"
        cp -ru "${FC_SHARE}/." "${BUILD_DIR}/share/fontconfig/"
        echo "  fontconfig: conf.avail deployed"
    fi

    echo "==> Deploy complete."
else
    echo "==> WARN: Qt bin dir not found at ${QT_BIN} — skipping DLL deploy."
fi

echo "==> Done: ${BUILD_DIR}/OpenPDFStudio.exe"
