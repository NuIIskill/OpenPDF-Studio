#!/usr/bin/env bash
# Build OpenPDF Studio for Linux (Qt6 / CMake)
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"
TOOLS_DIR="${TOOLS_DIR:-${HOME}/.local/bin}"

BUILD_APPIMAGE=0
CMAKE_ARGS=()
for arg in "$@"; do
    if [ "$arg" = "--appimage" ]; then
        BUILD_APPIMAGE=1
    else
        CMAKE_ARGS+=("$arg")
    fi
done

echo "==> Configuring (${BUILD_TYPE}) → ${BUILD_DIR}/"
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    "${CMAKE_ARGS[@]}"

echo "==> Building with ${JOBS} job(s)…"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

echo "==> Done: ${BUILD_DIR}/OpenPDFStudio"

if [ "${BUILD_APPIMAGE}" = "1" ]; then
    echo "==> Packaging AppImage…"
    APPDIR="${BUILD_DIR}/AppDir"
    rm -rf "${APPDIR}"

    # Install into AppDir with /usr prefix
    cmake -S . -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_INSTALL_PREFIX=/usr \
        "${CMAKE_ARGS[@]}"
    cmake --build "${BUILD_DIR}" -j "${JOBS}"
    DESTDIR="${APPDIR}" cmake --install "${BUILD_DIR}"

    # ── Locate or download tools ──────────────────────────────────────────────
    _need_tool() {
        local name="$1" url="$2"
        if command -v "${name}" &>/dev/null; then echo "${name}"; return; fi
        local dest="${TOOLS_DIR}/${name}"
        if [ -x "${dest}" ]; then echo "${dest}"; return; fi
        echo "==> Downloading ${name}…" >&2
        mkdir -p "${TOOLS_DIR}"
        if command -v curl &>/dev/null; then
            curl -fsSL -o "${dest}" "${url}"
        elif command -v wget &>/dev/null; then
            wget -q -O "${dest}" "${url}"
        else
            echo "ERROR: neither curl nor wget found." >&2; exit 1
        fi
        chmod +x "${dest}"
        echo "${dest}"
    }

    ARCH="${ARCH:-x86_64}"
    LINUXDEPLOY=$(_need_tool "linuxdeploy-${ARCH}.AppImage" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage")
    QT_PLUGIN=$(_need_tool "linuxdeploy-plugin-qt-${ARCH}.AppImage" \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage")
    APPIMAGETOOL=$(_need_tool "appimagetool-${ARCH}.AppImage" \
        "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage")
    export LINUXDEPLOY_PLUGIN_QT="${QT_PLUGIN}"

    # appimagetool lädt die type2-Runtime bei JEDEM Lauf neu von GitHub und
    # bricht ab, wenn das fehlschlägt ("server returned status code 0").
    # Einmal cachen und per --runtime-file reinreichen macht den Pack-Schritt
    # unabhängig davon.
    RUNTIME_FILE="${TOOLS_DIR}/appimage-runtime-${ARCH}"
    if [ ! -s "${RUNTIME_FILE}" ]; then
        RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-${ARCH}"
        echo "==> Downloading AppImage runtime…" >&2
        mkdir -p "${TOOLS_DIR}"
        if command -v curl &>/dev/null; then
            curl -fsSL --retry 3 -o "${RUNTIME_FILE}" "${RUNTIME_URL}" || rm -f "${RUNTIME_FILE}"
        elif command -v wget &>/dev/null; then
            wget -q -O "${RUNTIME_FILE}" "${RUNTIME_URL}" || rm -f "${RUNTIME_FILE}"
        fi
    fi

    # ── Pre-seed Wayland Qt plugins + libs before linuxdeploy resolves deps ──
    QT_INSTALL_PLUGINS=$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || \
                         qmake  -query QT_INSTALL_PLUGINS 2>/dev/null || \
                         echo "/usr/lib64/qt6/plugins")
    QT_LIB_DIR_PRE=$(qmake6 -query QT_INSTALL_LIBS 2>/dev/null || \
                     qmake  -query QT_INSTALL_LIBS 2>/dev/null || \
                     echo "/usr/lib64")
    _copy_qt_plugin() {
        local rel="$1"
        local src="${QT_INSTALL_PLUGINS}/${rel}"
        local dst="${APPDIR}/usr/plugins/$(dirname "${rel}")"
        [ -f "${src}" ] && { mkdir -p "${dst}"; cp -n "${src}" "${dst}/"; }
    }
    _copy_qt_lib() {
        local name="$1"
        local src; src=$(find "${QT_LIB_DIR_PRE}" /lib64 /usr/lib64 \
                              -maxdepth 2 -name "${name}" 2>/dev/null | head -1)
        [ -f "${src}" ] && { mkdir -p "${APPDIR}/usr/lib"; cp -n "${src}" "${APPDIR}/usr/lib/"; }
    }
    _copy_qt_plugin "platforms/libqwayland.so"
    _copy_qt_plugin "wayland-graphics-integration-client/libqt-plugin-wayland-egl.so"
    # Bundle the Wayland client lib so plugins don't pull in the system Qt6
    _copy_qt_lib "libQt6WaylandClient.so.6"

    # ── Deploy dependencies (no packaging yet) ────────────────────────────────
    # NO_STRIP=1: bundled strip doesn't understand .relr.dyn on Fedora 40+.
    NO_STRIP=1 "${LINUXDEPLOY}" \
        --appdir "${APPDIR}" \
        --plugin qt

    # ── Fix patchelf-corrupted libs ───────────────────────────────────────────
    # linuxdeploy bundles patchelf 0.15.0, which moves the .init section when
    # adding RPATH but forgets to update DT_INIT. The dynamic linker then calls
    # base+0x2cc (ELF header) instead of the real _init → segfault.
    # Fix: re-copy each bundled lib from its source and re-patch with the system
    # patchelf (0.18.0+) which correctly updates all affected pointers.
    SYS_PATCHELF="$(command -v patchelf 2>/dev/null)"
    if [ -n "${SYS_PATCHELF}" ]; then
        echo "==> Re-patching bundled libs with system $(${SYS_PATCHELF} --version)…"
        QT_LIB_DIR=$(qmake6 -query QT_INSTALL_LIBS 2>/dev/null || \
                     qmake  -query QT_INSTALL_LIBS 2>/dev/null || \
                     echo "/usr/lib64")
        _repatch_lib() {
            local bundled_lib="$1" rpath="${2:-\$ORIGIN}"
            [ -f "${bundled_lib}" ] || return
            local libname; libname="$(basename "${bundled_lib}")"
            # Search only 64-bit dirs to avoid multiarch 32-bit libs in /usr/lib
            local src; src=$(find "${QT_LIB_DIR}" /lib64 /usr/lib64 \
                                  -maxdepth 2 -name "${libname}" 2>/dev/null | head -1)
            if [ -n "${src}" ] && [ -f "${src}" ]; then
                cp -f "${src}" "${bundled_lib}"
                "${SYS_PATCHELF}" --set-rpath "${rpath}" "${bundled_lib}" 2>/dev/null || true
            fi
        }
        for bundled_lib in "${APPDIR}"/usr/lib/lib*.so*; do
            _repatch_lib "${bundled_lib}" '$ORIGIN'
        done
        # Plugins: re-copy from QT_INSTALL_PLUGINS to fix DT_INIT corruption,
        # then set RPATH so they find bundled Qt6 libs in usr/lib/.
        # (patchelf 0.15.0 moves .init but forgets to update DT_INIT in plugins
        # too — lazily-loaded plugins crash the same way as libs at startup)
        QT_PLUGIN_SRC=$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || \
                        qmake  -query QT_INSTALL_PLUGINS 2>/dev/null || \
                        echo "/usr/lib64/qt6/plugins")
        for plugin in "${APPDIR}"/usr/plugins/*/*.so; do
            [ -f "${plugin}" ] || continue
            rel="${plugin#${APPDIR}/usr/plugins/}"
            src="${QT_PLUGIN_SRC}/${rel}"
            if [ -f "${src}" ]; then
                cp -f "${src}" "${plugin}"
            fi
            "${SYS_PATCHELF}" --set-rpath '$ORIGIN:$ORIGIN/../../lib' \
                "${plugin}" 2>/dev/null || true
        done
    else
        echo "WARNING: system patchelf not found — AppImage may crash (install patchelf >= 0.18)" >&2
    fi

    # ── Remove X11/XCB (Wayland-only app) ────────────────────────────────────
    echo "==> Stripping X11 artefacts (Wayland-only)…"
    rm -f  "${APPDIR}"/usr/plugins/platforms/libqxcb.so
    rm -rf "${APPDIR}"/usr/plugins/xcbglintegrations
    find   "${APPDIR}"/usr/lib -maxdepth 1 \
           \( -name "libX*.so*" -o -name "libxcb*.so*" \) \
           -delete 2>/dev/null || true

    # ── Remove libs that must come from the host system ──────────────────────
    # libffi is used by the *system* libwayland-client via ffi_call to invoke
    # Qt closure callbacks. LD_LIBRARY_PATH causes it to pick up the bundled
    # libffi instead of the system one → ABI mismatch → SIGSEGV in file dialogs.
    # Rule: any lib that is dlopen'd or DT_NEEDED by a NON-bundled system lib
    # must not be bundled.
    rm -f "${APPDIR}/usr/lib/libffi.so.8"

    # ── Remove qt.conf (AppRun handles all Qt paths via env vars) ────────────
    # linuxdeploy-plugin-qt generates qt.conf with "Prefix = ../" which breaks
    # Wayland platform init even when QT_PLUGIN_PATH is set. Remove it so Qt
    # falls back to QT_PLUGIN_PATH / LD_LIBRARY_PATH set by AppRun.
    rm -f "${APPDIR}/usr/bin/qt.conf"

    # ── Write proper AppRun (replaces linuxdeploy's symlink) ─────────────────
    # IMPORTANT: remove the symlink first — cat > follows symlinks and would
    # overwrite the binary instead of creating a new AppRun file.
    rm -f "${APPDIR}/AppRun"
    cat > "${APPDIR}/AppRun" << 'APPRUN_EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
exec "${HERE}/usr/bin/OpenPDFStudio" "$@"
APPRUN_EOF
    chmod +x "${APPDIR}/AppRun"

    # ── Pack ──────────────────────────────────────────────────────────────────
    echo "==> Packing AppImage…"
    mkdir -p dist
    VERSION="$(tr -d '[:space:]' < version.txt)"
    APPIMAGE_OUT="dist/OpenPDF_Studio-${VERSION}-${ARCH}.AppImage"
    APPIMAGETOOL_ARGS=()
    [ -s "${RUNTIME_FILE}" ] && APPIMAGETOOL_ARGS+=(--runtime-file "${RUNTIME_FILE}")
    ARCH="${ARCH}" "${APPIMAGETOOL}" "${APPIMAGETOOL_ARGS[@]}" "${APPDIR}" "${APPIMAGE_OUT}"
    echo "==> AppImage ready: ${APPIMAGE_OUT}"
fi
