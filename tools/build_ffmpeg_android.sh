#!/bin/sh
# Compila o FFmpeg 8.1 (LGPL) mínimo para o Android: só o que os vídeos do jogo usam (WMV: ASF + VC-1/WMV3 + WMA Pro),
# mais swscale e swresample. Gera bibliotecas compartilhadas em tools/ffmpeg-android/<abi>.
# Uso (Git Bash): tools/build_ffmpeg_android.sh [arm64-v8a] [x86_64]
set -e
export TMPDIR=$(cd "$(dirname "$0")" && pwd)/ffmpeg-src/tmp; mkdir -p "$TMPDIR"
ROOT=$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")
SRC=$ROOT/tools/ffmpeg-src/ffmpeg-8.1
NDK=${ANDROID_NDK:-$LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358}
NDK=$(cygpath -m "$NDK")
TC=$NDK/toolchains/llvm/prebuilt/windows-x86_64
API=30
# o make do NDK é nativo do Windows: os caminhos passados ao configure vão no formato C:/..., só o PATH fica no do shell
# (o NDK vem por último: o cmp.exe dele não entende os caminhos do shell e quebra o configure)
export PATH=$PATH:$(cygpath -u "$TC")/bin:$(cygpath -u "$NDK")/prebuilt/windows-x86_64/bin
MAKE=$(cygpath -u "$NDK")/prebuilt/windows-x86_64/bin/make.exe

for ABI in ${@:-arm64-v8a x86_64}; do
    case $ABI in
        arm64-v8a) ARCH=aarch64; TRIPLE=aarch64-linux-android; EXTRA= ;;
        x86_64)    ARCH=x86_64;  TRIPLE=x86_64-linux-android;  EXTRA=--disable-x86asm ;;
        *) echo "ABI desconhecida: $ABI"; exit 1 ;;
    esac
    OUT=$ROOT/tools/ffmpeg-android/$ABI
    BUILD=$ROOT/tools/ffmpeg-src/build-$ABI
    rm -rf "$BUILD"; mkdir -p "$BUILD"; cd "$BUILD"
    "$SRC/configure" \
        --prefix="$OUT" --target-os=android --arch=$ARCH --enable-cross-compile \
        --cc="$TC/bin/clang.exe" --cxx="$TC/bin/clang++.exe" --ar="$TC/bin/llvm-ar.exe" --nm="$TC/bin/llvm-nm.exe" \
        --ranlib="$TC/bin/llvm-ranlib.exe" --strip="$TC/bin/llvm-strip.exe" --host-cc="$TC/bin/clang.exe" \
        --host-cflags="--target=x86_64-linux-android$API --sysroot=$TC/sysroot" \
        --sysroot="$TC/sysroot" \
        --extra-cflags="--target=$TRIPLE$API -O2 -fPIC" --extra-ldflags="--target=$TRIPLE$API" \
        --enable-shared --disable-static --enable-pic --disable-programs --disable-doc --disable-avdevice \
        --disable-avfilter --disable-network --disable-everything --disable-autodetect --disable-debug \
        --enable-protocol=file --enable-demuxer=asf \
        --enable-decoder=vc1,wmv3,wmv2,wmv1,wmapro,wmav2,wmav1 --enable-parser=vc1 \
        --enable-swscale --enable-swresample $EXTRA
    # o configure grava o caminho do código no formato /c/...; o make nativo precisa de C:/...
    sed -i 's|/\([a-zA-Z]\)/Users/|\1:/Users/|g' Makefile ffbuild/config.mak
    "$MAKE" -j"$(nproc)"
    "$MAKE" install || true
    # a lista de cabeçalhos da libavutil estoura a linha de comando do Windows no install: copia à mão
    mkdir -p "$OUT/include/libavutil"
    for f in $(sed -n '/^HEADERS = /,/^$/p' "$SRC/libavutil/Makefile" | grep -oE '[a-z0-9_]+\.h') avconfig.h ffversion.h; do
        if [ -f "libavutil/$f" ]; then cp "libavutil/$f" "$OUT/include/libavutil/"; elif [ -f "$SRC/libavutil/$f" ]; then cp "$SRC/libavutil/$f" "$OUT/include/libavutil/"; fi
    done
done
