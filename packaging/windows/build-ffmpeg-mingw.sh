#!/usr/bin/env bash
# Cross-builds a small, static ffmpeg.exe for Windows x86_64 (mingw-w64) into
# the project-local prefix, from where build-win.sh copies it next to the
# program.
#
# It covers what Media Foundation cannot read: AC3 sound, and AV1, which
# Windows decodes only with a graphics adapter. LGPL only: no --enable-gpl and
# no x264; AV1 comes from dav1d (BSD-2-Clause). H.264 is encoded by Windows'
# own encoder through h264_mf, AAC by ffmpeg's native encoder.
#
#   packaging/windows/build-ffmpeg-mingw.sh          # builds when missing
#   packaging/windows/build-ffmpeg-mingw.sh --force  # always rebuilds
set -euo pipefail

FFMPEG_VERSION="7.1.1"
FFMPEG_SHA256="733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1"
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
NASM_VERSION="2.16.03"
NASM_SHA256="1412a1c760bbd05db026b6c0d1657affd6631cd0a63cddb6f73cc6d4aa616148"
NASM_URL="https://www.nasm.us/pub/nasm/releasebuilds/${NASM_VERSION}/nasm-${NASM_VERSION}.tar.xz"
DAV1D_VERSION="1.5.1"
DAV1D_SHA256="401813f1f89fa8fd4295805aa5284d9aed9bc7fc1fdbe554af4292f64cbabe21"
DAV1D_URL="https://downloads.videolan.org/pub/videolan/dav1d/${DAV1D_VERSION}/dav1d-${DAV1D_VERSION}.tar.xz"
MESON_VERSION="1.6.1"
MESON_SHA256="1eca49eb6c26d58bbee67fd3337d8ef557c0804e30a6d16bfdf269db997464de"
MESON_URL="https://github.com/mesonbuild/meson/releases/download/${MESON_VERSION}/meson-${MESON_VERSION}.tar.gz"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${FFMPEG_MINGW_PREFIX:-$ROOT/third_party/mingw64}"
CACHE="$ROOT/third_party/.cache"
JOBS="${JOBS:-$(nproc)}"

FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

if [ "$FORCE" = 0 ] && [ -f "$PREFIX/bin/ffmpeg.exe" ]; then
    echo "==> ffmpeg.exe is already in $PREFIX/bin (--force rebuilds)"
    exit 0
fi

command -v x86_64-w64-mingw32-gcc >/dev/null || {
    echo "ERROR: mingw-w64 gcc not found." >&2
    echo "  On Fedora: sudo dnf install mingw64-gcc mingw64-winpthreads-static" >&2
    exit 1
}

fetch() {
    local url="$1" sum="$2" file="$CACHE/$(basename "$1")"
    if [ ! -f "$file" ]; then
        echo "==> Downloading $(basename "$file")..."
        curl -fsSL --retry 3 -o "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    echo "${sum}  ${file}" | sha256sum -c - >/dev/null || {
        echo "ERROR: checksum mismatch, delete and download again:" >&2
        echo "  rm $file" >&2
        exit 1
    }
}

mkdir -p "$CACHE"
fetch "$FFMPEG_URL" "$FFMPEG_SHA256"
fetch "$NASM_URL" "$NASM_SHA256"
fetch "$DAV1D_URL" "$DAV1D_SHA256"
fetch "$MESON_URL" "$MESON_SHA256"

# nasm is only a build tool; without it the decoders lose their assembly and
# get several times slower.
NASM_DIR="$CACHE/nasm-${NASM_VERSION}"
if [ ! -x "$NASM_DIR/nasm" ]; then
    echo "==> Building nasm ${NASM_VERSION}..."
    rm -rf "$NASM_DIR"
    tar xJf "$CACHE/nasm-${NASM_VERSION}.tar.xz" -C "$CACHE"
    (cd "$NASM_DIR" && ./configure >/dev/null && make -j"$JOBS" nasm >/dev/null)
fi
export PATH="$NASM_DIR:$PATH"

# dav1d is built with meson, which is only a build tool here as well.
MESON_DIR="$CACHE/meson-${MESON_VERSION}"
[ -f "$MESON_DIR/meson.py" ] || tar xzf "$CACHE/meson-${MESON_VERSION}.tar.gz" -C "$CACHE"

echo "==> Building dav1d ${DAV1D_VERSION}..."
DAV1D_SRC="$CACHE/dav1d-${DAV1D_VERSION}"
DAV1D_BUILD="$CACHE/build-dav1d-mingw"
DAV1D_PREFIX="$CACHE/dav1d-mingw"
rm -rf "$DAV1D_SRC" "$DAV1D_BUILD" "$DAV1D_PREFIX"
tar xJf "$CACHE/dav1d-${DAV1D_VERSION}.tar.xz" -C "$CACHE"
printf '%s\n' \
    "[binaries]" \
    "c = 'x86_64-w64-mingw32-gcc'" \
    "ar = 'x86_64-w64-mingw32-ar'" \
    "strip = 'x86_64-w64-mingw32-strip'" \
    "windres = 'x86_64-w64-mingw32-windres'" \
    "nasm = '$NASM_DIR/nasm'" \
    "" \
    "[host_machine]" \
    "system = 'windows'" \
    "cpu_family = 'x86_64'" \
    "cpu = 'x86_64'" \
    "endian = 'little'" > "$CACHE/mingw64-cross.ini"
python3 "$MESON_DIR/meson.py" setup "$DAV1D_BUILD" "$DAV1D_SRC" \
    --cross-file "$CACHE/mingw64-cross.ini" --prefix "$DAV1D_PREFIX" \
    --buildtype release --default-library static \
    -Denable_tools=false -Denable_tests=false >/dev/null
ninja -C "$DAV1D_BUILD" install >/dev/null

SRC="$CACHE/ffmpeg-${FFMPEG_VERSION}"
rm -rf "$SRC"
tar xJf "$CACHE/ffmpeg-${FFMPEG_VERSION}.tar.xz" -C "$CACHE"

VIDEO_DECODERS="h264,hevc,mpeg4,msmpeg4v1,msmpeg4v2,msmpeg4v3,mpeg1video,mpeg2video,\
mjpeg,wmv1,wmv2,wmv3,vc1,vp8,vp9,theora,flv,h263,h263p,prores,dnxhd,rawvideo,\
cinepak,svq1,svq3,dvvideo,huffyuv,ffv1,png,qtrle,indeo3,indeo4,indeo5,libdav1d"
AUDIO_DECODERS="aac,aac_latm,ac3,eac3,dca,truehd,mlp,mp1,mp2,mp3,mp3float,opus,\
vorbis,flac,alac,wmav1,wmav2,wmapro,wmavoice,amrnb,amrwb,adpcm_ms,adpcm_ima_wav,\
adpcm_ima_qt,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_f32le,pcm_u8,\
pcm_alaw,pcm_mulaw,gsm_ms"
DEMUXERS="avi,asf,matroska,mov,flv,mpegps,mpegts,mpegvideo,ogg,wav,mp3,aac,ac3,\
eac3,h264,hevc,m4v,dv,mxf,rm,nut,ivf,webm_dash_manifest"
PARSERS="h264,hevc,av1,mpeg4video,mpegvideo,vc1,vp8,vp9,mjpeg,aac,aac_latm,ac3,dca,\
mlp,mpegaudio,opus,vorbis,flac"

echo "==> Configuring ffmpeg ${FFMPEG_VERSION}..."
BUILD="$CACHE/build-ffmpeg-mingw"
rm -rf "$BUILD"
mkdir -p "$BUILD"
(cd "$BUILD" && PKG_CONFIG_LIBDIR="$DAV1D_PREFIX/lib/pkgconfig" "$SRC/configure" \
    --prefix="$PREFIX" \
    --target-os=mingw32 --arch=x86_64 \
    --cross-prefix=x86_64-w64-mingw32- --enable-cross-compile \
    --pkg-config=pkg-config --pkg-config-flags=--static \
    --enable-static --disable-shared \
    --extra-ldflags="-static" \
    --disable-autodetect --enable-mediafoundation --enable-w32threads \
    --enable-libdav1d \
    --disable-everything --disable-network --disable-doc --disable-debug \
    --disable-ffplay --disable-ffprobe \
    --enable-protocol=file,pipe \
    --enable-decoder="$VIDEO_DECODERS,$AUDIO_DECODERS" \
    --enable-demuxer="$DEMUXERS" \
    --enable-parser="$PARSERS" \
    --enable-encoder=h264_mf,aac \
    --enable-muxer=mp4,mov \
    --enable-bsf=aac_adtstoasc,h264_mp4toannexb,extract_extradata,vp9_superframe_split \
    --enable-filter=scale,format,aformat,aresample,null,anull,copy,setsar,fps \
    --enable-swscale --enable-swresample --enable-avfilter >/dev/null)

grep -q "^CONFIG_GPL=yes" "$BUILD/ffbuild/config.mak" && {
    echo "ERROR: the configuration came out GPL; it must stay LGPL." >&2
    exit 1
}

echo "==> Building..."
make -C "$BUILD" -j"$JOBS" >/dev/null
make -C "$BUILD" install >/dev/null
x86_64-w64-mingw32-strip "$PREFIX/bin/ffmpeg.exe"

echo "==> Done: $PREFIX/bin/ffmpeg.exe ($(du -h "$PREFIX/bin/ffmpeg.exe" | cut -f1))"
