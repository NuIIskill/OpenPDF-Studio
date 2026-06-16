#!/usr/bin/env bash
# Cross-compile OpenPDF Studio for Windows (x86_64) using MXE or a native toolchain.
#
# Prerequisites (MXE example):
#   sudo apt install mxe  (or clone https://mxe.cc)
#   MXE_ROOT=/opt/mxe
#   ${MXE_ROOT}/usr/bin/x86_64-w64-mingw32.static-cmake  must be on PATH
#
# Alternatively, set TOOLCHAIN_FILE to a CMake toolchain file for your
# cross-compiler / vcpkg / MSYS2 / Qt-for-Windows installation.
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build-win}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"

# ── Toolchain detection ────────────────────────────────────────────────────────
if [[ -n "${TOOLCHAIN_FILE:-}" ]]; then
    TOOLCHAIN_ARG="-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"
elif command -v x86_64-w64-mingw32.static-cmake &>/dev/null; then
    # MXE static toolchain
    TOOLCHAIN_ARG="-DCMAKE_TOOLCHAIN_FILE=$(x86_64-w64-mingw32.static-cmake --print-cmake-toolchain)"
else
    echo "ERROR: No Windows cross-compile toolchain found."
    echo "  • Set TOOLCHAIN_FILE=/path/to/toolchain.cmake, or"
    echo "  • Install MXE and ensure x86_64-w64-mingw32.static-cmake is on PATH."
    exit 1
fi

echo "==> Configuring (${BUILD_TYPE}) → ${BUILD_DIR}/"
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    ${TOOLCHAIN_ARG} \
    "${@}"

echo "==> Building with ${JOBS} job(s)…"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

echo "==> Done: ${BUILD_DIR}/OpenPDFStudio.exe"
