#!/usr/bin/env bash
# Build OpenPDF Studio for Linux + optional package formats.
#
# Usage:
#   ./build.sh                         # compile only
#   ./build.sh --appimage              # compile + AppImage
#   ./build.sh --deb                   # compile + .deb (Ubuntu/Debian)
#   ./build.sh --rpm                   # compile + .rpm (Fedora/RHEL)
#   ./build.sh --all                   # compile + all three formats
#   ./build.sh -DCMAKE_BUILD_TYPE=Debug --deb
#
# Any argument not starting with '--' is forwarded to cmake.
set -euo pipefail

# ── Configuration ─────────────────────────────────────────────────────────────
VERSION=$(tr -d '[:space:]' < "$(dirname "$0")/version.txt")
APP_NAME="OpenPDFStudio"
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"
TOOLS_DIR="$(pwd)/tools"      # cached AppImage download tools
DIST_DIR="$(pwd)/dist"        # output packages land here

# ── Argument parsing ──────────────────────────────────────────────────────────
DO_APPIMAGE=0
DO_DEB=0
DO_RPM=0
CMAKE_EXTRA=()

for arg in "$@"; do
    case "$arg" in
        --appimage) DO_APPIMAGE=1 ;;
        --deb)      DO_DEB=1      ;;
        --rpm)      DO_RPM=1      ;;
        --all)      DO_APPIMAGE=1; DO_DEB=1; DO_RPM=1 ;;
        *)          CMAKE_EXTRA+=("$arg") ;;
    esac
done

# ── 1. Compile ────────────────────────────────────────────────────────────────
echo "==> Configuring (${BUILD_TYPE}) → ${BUILD_DIR}/"
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    "${CMAKE_EXTRA[@]}"

echo "==> Building with ${JOBS} job(s)…"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

if [[ $DO_APPIMAGE -eq 0 && $DO_DEB -eq 0 && $DO_RPM -eq 0 ]]; then
    echo "==> Done: ${BUILD_DIR}/${APP_NAME}"
    echo "    Use --appimage, --deb, --rpm, or --all to create packages."
    exit 0
fi

mkdir -p "${DIST_DIR}"

# ── Helper: cmake install into a staging root ─────────────────────────────────
stage_install() {
    local destdir="$1"
    rm -rf "${destdir}"
    DESTDIR="${destdir}" cmake --install "${BUILD_DIR}" --prefix /usr
}

# ── 2. AppImage ───────────────────────────────────────────────────────────────
build_appimage() {
    echo ""
    echo "==> Building AppImage…"
    mkdir -p "${TOOLS_DIR}"

    local LD="${TOOLS_DIR}/linuxdeploy-x86_64.AppImage"
    local LDQ="${TOOLS_DIR}/linuxdeploy-plugin-qt-x86_64.AppImage"
    local AIT="${TOOLS_DIR}/appimagetool-x86_64.AppImage"

    _fetch() { [[ -x "$1" ]] || { echo "  Downloading $(basename "$1")…"; curl -fsSL -o "$1" "$2"; chmod +x "$1"; }; }
    _fetch "$LD"  "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    _fetch "$LDQ" "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    _fetch "$AIT" "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"

    local APPDIR="$(pwd)/AppDir"
    stage_install "${APPDIR}"

    export QML_SOURCES_PATHS=.
    export OUTPUT="${DIST_DIR}/${APP_NAME}-${VERSION}-x86_64.AppImage"
    export NO_STRIP=1                  # linuxdeploy's bundled strip is too old for Fedora's RELR relocations
    export APPIMAGE_EXTRACT_AND_RUN=1  # run AppImage tools without FUSE

    # Step 1: deploy dependencies (without packaging so we can post-process)
    "$LD" \
        --appdir "${APPDIR}" \
        --desktop-file packaging/linux/openpdf-studio.desktop \
        --icon-file    packaging/linux/icons/256/openpdf-studio.png \
        --plugin qt

    # Step 2a: linuxdeploy-plugin-qt patches Qt6 libs which corrupts them on Fedora 44
    # (RELR relocations). Remove ALL bundled Qt6 libs — the binary's RUNPATH is
    # $ORIGIN/../lib; when the libs aren't there the linker falls back to system paths.
    # This also ensures every Qt6 plugin loads the SAME Qt6 instance (system), avoiding
    # the two-instance problem that breaks QFileDialog / xdgdesktopportal.
    rm -f "${APPDIR}/usr/lib/libQt6"*.so.*

    # Qt6 plugins: replace with unpatched system versions.
    # RPATH stays empty — with Qt6 libs removed from bundle, plugins find Qt6 via
    # the linker's default system search path (/lib64), same instance as the binary.
    find "${APPDIR}/usr/plugins" -name "*.so" | while read -r _plugin; do
        _bname=$(basename "${_plugin}")
        _sysplug=$(find /lib64/qt6/plugins -name "${_bname}" 2>/dev/null | head -1)
        [[ -n "$_sysplug" ]] && cp "${_sysplug}" "${_plugin}"
    done

    # Deploy platform theme plugins (linuxdeploy-plugin-qt may skip platformthemes/).
    mkdir -p "${APPDIR}/usr/plugins/platformthemes"
    for _pt in libqxdgdesktopportal.so libqgtk3.so libqgtk4.so; do
        _syspt=$(find /lib64/qt6/plugins/platformthemes -name "${_pt}" 2>/dev/null | head -1)
        [[ -n "$_syspt" ]] && cp "${_syspt}" "${APPDIR}/usr/plugins/platformthemes/${_pt}"
    done

    # Step 2b: remove unneeded heavy plugins and system-specific libs.

    # Virtual keyboard — pulls in Qt6Quick/QML (~50 MB), initialises on first text widget.
    rm -f "${APPDIR}/usr/plugins/platforminputcontexts/libqtvirtualkeyboardplugin.so"
    # IBus input context plugin — tries to connect to IBus via D-Bus when a text field
    # gets focus; causes ~1 s delay if IBus is not running. Note: Fedora names the
    # plugin without the "q" prefix: libibus... not libqibus...
    rm -f "${APPDIR}/usr/plugins/platforminputcontexts/libibusplatforminputcontextplugin.so"
    rm -f "${APPDIR}/usr/plugins/platforminputcontexts/libqfcitxplatforminputcontextplugin.so"
    rm -f "${APPDIR}/usr/plugins/platforminputcontexts/libqfcitx5platforminputcontextplugin.so"
    for _qml in libQt6VirtualKeyboard libQt6Quick libQt6Qml libQt6QmlMeta libQt6QmlModels libQt6QmlWorkerScript libQt6OpenGL; do
        rm -f "${APPDIR}/usr/lib/${_qml}.so."*
    done

    # Network information plugins — we don't use networking; these try D-Bus on first
    # plugin-dir scan. libqconnman.so times out (~1 s) because ConnMan is not running
    # on Fedora/GNOME systems. Drop all three, Qt falls back to "none" silently.
    rm -f "${APPDIR}/usr/plugins/networkinformation/libqconnman.so"
    rm -f "${APPDIR}/usr/plugins/networkinformation/libqnetworkmanager.so"
    rm -f "${APPDIR}/usr/plugins/networkinformation/libqglib.so"

    # OpenSSL TLS plugin — libssl/libcrypto are removed from the bundle (system-specific
    # FIPS/provider paths crash on Fedora). Without those libs the plugin fails to dlopen,
    # triggering a slow error path. Remove it; Qt falls back to libqcertonlybackend.so.
    rm -f "${APPDIR}/usr/plugins/tls/libqopensslbackend.so"

    # System-specific libs that must NOT be bundled.
    # These interact with the running kernel/services and crash when their ABI
    # doesn't exactly match the host system.
    local _SYSTEM_LIBS=(
        libudev.so.1          # udev — must match running kernel/systemd
        libsystemd.so.0       # systemd
        libcrypt.so.2         # libxcrypt — glibc-adjacent, system-specific
        libdbus-1.so.3        # D-Bus — must match running dbus daemon
        libselinux.so.1       # SELinux — kernel-specific
        libblkid.so.1         # block device IDs — system-specific
        libmount.so.1         # libmount — system-specific
        libfido2.so.1         # FIDO2 hardware keys — pulled in by libcurl
        libcbor.so.0.13       # CBOR — dep of libfido2
        libkrb5.so.3          # Kerberos — system auth
        libk5crypto.so.3
        libkrb5support.so.0
        libkeyutils.so.1
        libgssapi_krb5.so.2
        libsasl2.so.3         # SASL — system auth
        libldap.so.2          # LDAP
        liblber.so.2
        libevent-2.1.so.7     # dep of libldap
        libproxy.so.1         # libproxy — reads system proxy config
        libpxbackend-1.0.so   # libproxy GLib backend plugin
        libcrypto.so.3        # OpenSSL — Fedora-specific provider/FIPS paths cause crash
        libssl.so.3           # OpenSSL — same reason, rely on system OpenSSL
        libunistring.so.5     # Unicode — ABI mismatch with system version causes crash in init
    )
    for _lib in "${_SYSTEM_LIBS[@]}"; do
        rm -f "${APPDIR}/usr/lib/${_lib}"
    done

    # Step 3: AppRun — set Qt plugin path but NOT LD_LIBRARY_PATH.
    # The binary loads Qt6 via RUNPATH ($ORIGIN/../lib) and those Qt6 libs
    # load their own transitive deps (glib, pcre2, etc.) from the system.
    # Adding LD_LIBRARY_PATH forces transitive deps to load from the bundle,
    # which crashes on Fedora 44 due to RELR/ABI differences.
    rm -f "${APPDIR}/AppRun"
    cat > "${APPDIR}/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export QT_QPA_PLATFORM_PLUGIN_PATH="${HERE}/usr/plugins/platforms"

# Force xdgdesktopportal platform theme so Qt uses the native GNOME file picker.
# Without this, auto-detection can fail inside AppImage and Qt falls back to its
# own file dialog.
export QT_QPA_PLATFORMTHEME=xdgdesktopportal

# AT-SPI accessibility bridge registers every widget with D-Bus on each new window,
# causing ~1 s delay per dialog on GNOME. Disable it for the AppImage.
export NO_AT_BRIDGE=1

# Create a desktop file so the XDG portal can match our app ID
# (QApplication::setDesktopFileName) and so GNOME Shell shows the correct icon
# via StartupWMClass matching. Icon uses the absolute mount path — this is updated
# each launch so GNOME always reads the correct image.
_DESK_DIR="${HOME}/.local/share/applications"
mkdir -p "${_DESK_DIR}"
cat > "${_DESK_DIR}/io.openpdfstudio.OpenPDFStudio.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=OpenPDF Studio
Icon=${HERE}/usr/share/icons/hicolor/256x256/apps/openpdf-studio.png
Exec=${HERE}/usr/bin/OpenPDFStudio
Categories=Office;
StartupWMClass=OpenPDFStudio
NoDisplay=true
DESK
update-desktop-database "${_DESK_DIR}" 2>/dev/null || true

exec "${HERE}/usr/bin/OpenPDFStudio" "$@"
APPRUN
    chmod +x "${APPDIR}/AppRun"

    # Step 4: pack with appimagetool
    "$AIT" "${APPDIR}" "${OUTPUT}"

    echo "  → ${OUTPUT}"
    rm -rf "${APPDIR}"
}

# ── 3. DEB package ────────────────────────────────────────────────────────────
build_deb() {
    echo ""
    echo "==> Building .deb package…"

    local PKGROOT="$(pwd)/_deb_pkg"
    stage_install "${PKGROOT}"
    mkdir -p "${PKGROOT}/DEBIAN"

    local SIZE
    SIZE=$(du -sk "${PKGROOT}/usr" | cut -f1)

    # Control file — libqt6*t64 variants cover Ubuntu 24.04+
    cat > "${PKGROOT}/DEBIAN/control" <<EOF
Package: openpdf-studio
Version: ${VERSION}
Architecture: amd64
Maintainer: OpenPDF Studio Team <noreply@openpdfstudio.io>
Installed-Size: ${SIZE}
Depends: libqt6core6 (>= 6.4) | libqt6core6t64 (>= 6.4),
 libqt6gui6 (>= 6.4) | libqt6gui6t64 (>= 6.4),
 libqt6widgets6 (>= 6.4) | libqt6widgets6t64 (>= 6.4),
 libqt6svgwidgets6 (>= 6.4) | libqt6svgwidgets6t64 (>= 6.4),
 libqt6printsupport6 (>= 6.4) | libqt6printsupport6t64 (>= 6.4),
 libqt6pdf6 (>= 6.4) | libpoppler-qt6-3
Section: office
Priority: optional
Description: Modern PDF editor built with Qt6
 OpenPDF Studio lets you open, view, annotate and edit text in PDF
 documents. Supports multiple languages and multi-page documents.
EOF

    # Post-install: refresh desktop/icon caches
    cat > "${PKGROOT}/DEBIAN/postinst" <<'SCRIPT'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q || true
command -v gtk-update-icon-cache   >/dev/null 2>&1 && gtk-update-icon-cache -fqt /usr/share/icons/hicolor || true
SCRIPT
    chmod 0755 "${PKGROOT}/DEBIAN/postinst"

    local OUT="${DIST_DIR}/openpdf-studio_${VERSION}_amd64.deb"
    dpkg-deb --build --root-owner-group "${PKGROOT}" "${OUT}"
    echo "  → ${OUT}"
    rm -rf "${PKGROOT}"
}

# ── 4. RPM package ────────────────────────────────────────────────────────────
build_rpm() {
    echo ""
    echo "==> Building .rpm package…"

    local PKGROOT="$(pwd)/_rpm_pkg"
    stage_install "${PKGROOT}"

    local RPMROOT="${HOME}/.rpmbuild"
    mkdir -p "${RPMROOT}/"{BUILD,RPMS,SOURCES,SPECS,SRPMS}

    # Version-substitute spec and write it
    local SPEC="${RPMROOT}/SPECS/openpdf-studio.spec"
    sed "s/@VERSION@/${VERSION}/g" packaging/rpm/openpdf-studio.spec > "${SPEC}"

    rpmbuild -bb "${SPEC}" \
        --define "_topdir ${RPMROOT}" \
        --define "pkgroot $(realpath "${PKGROOT}")" \
        --nodeps \
        2>&1 | grep -Ev "^Processing files|^Executing"

    local RPM
    RPM=$(find "${RPMROOT}/RPMS" -name "openpdf-studio-${VERSION}*.rpm" | head -1 || true)
    if [[ -n "$RPM" ]]; then
        cp "$RPM" "${DIST_DIR}/"
        echo "  → ${DIST_DIR}/$(basename "$RPM")"
    else
        echo "  WARNING: RPM build may have failed; check ${RPMROOT}/RPMS"
    fi

    rm -rf "${PKGROOT}"
}

# ── Dispatch ──────────────────────────────────────────────────────────────────
[[ $DO_APPIMAGE -eq 1 ]] && build_appimage
[[ $DO_DEB      -eq 1 ]] && build_deb
[[ $DO_RPM      -eq 1 ]] && build_rpm

echo ""
echo "==> Packages written to ${DIST_DIR}/"
ls -lh "${DIST_DIR}/" 2>/dev/null || true
