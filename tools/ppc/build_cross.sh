#!/usr/bin/env bash
# powerpc-elf クロスツールチェーン (binutils 2.41 + GCC 13.2.0 + newlib 4.4.0 nano)。
# 版と newlib の nano 構成は tools/ci/build_cross.sh (i386-elf) と同じ。
# 違い: TARGET、--disable-multilib (既定の 32bit BE・ハード浮動小数点の 1 組だけ)、
# 既定の CPU は指定しない (-mcpu=7450 を既定にすると newlib に AltiVec が混ざるので、
# CPU はカーネルの CFLAGS で -mcpu を指定する)。
set -euo pipefail
TARGET=powerpc-elf
BINUTILS_VER=2.41; GCC_VER=13.2.0; NEWLIB_VER=4.4.0.20231231
BINUTILS_TAR=binutils-$BINUTILS_VER.tar.xz; GCC_TAR=gcc-$GCC_VER.tar.xz; NEWLIB_TAR=newlib-$NEWLIB_VER.tar.gz
BINUTILS_SHA256=ae9a5789e23459e59606e6714723f2d3ffc31c03174191ef0d015bdf06007450
GCC_SHA256=e275e76442a6067341a27f04c5c6b83d8613144004c0413528863dc6b5c743da
NEWLIB_SHA256=0c166a39e1bf0951dfafcd68949fe0e4b6d3658081d6282f39aeefc6310f2f13
PREFIX=${PREFIX:-$HOME/opt/cross-ppc}; SRC_DIR=${SRC_DIR:-$HOME/opt/src}; WORK=${WORK:-$HOME/os32-tmp/run/ppc-env/work}; JOBS=${JOBS:-3}
mkdir -p "$PREFIX" "$SRC_DIR" "$WORK"; export PATH="$PREFIX/bin:$PATH"
log() { echo "=== [$(date +%H:%M:%S)] $*"; }
fetch() { [ -f "$SRC_DIR/$1" ] || { curl -fL --retry 3 -o "$SRC_DIR/$1.part" "$2"; mv "$SRC_DIR/$1.part" "$SRC_DIR/$1"; }; echo "$3  $SRC_DIR/$1" | sha256sum -c -; }
fetch $BINUTILS_TAR https://ftp.gnu.org/gnu/binutils/$BINUTILS_TAR $BINUTILS_SHA256
fetch $GCC_TAR https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VER/$GCC_TAR $GCC_SHA256
fetch $NEWLIB_TAR https://sourceware.org/pub/newlib/$NEWLIB_TAR $NEWLIB_SHA256
cd "$WORK"; rm -rf binutils-$BINUTILS_VER gcc-$GCC_VER newlib-$NEWLIB_VER build-*
tar xf "$SRC_DIR/$BINUTILS_TAR"; tar xf "$SRC_DIR/$GCC_TAR"; tar xf "$SRC_DIR/$NEWLIB_TAR"
log "1/4 binutils"; mkdir build-binutils; cd build-binutils
../binutils-$BINUTILS_VER/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror --disable-multilib
make -j$JOBS; make install; cd "$WORK"
log "2/4 gcc stage1"; mkdir build-gcc1; cd build-gcc1
../gcc-$GCC_VER/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c --without-headers --disable-multilib
make -j$JOBS all-gcc all-target-libgcc; make install-gcc install-target-libgcc; cd "$WORK"
log "3/4 newlib nano"; mkdir build-newlib; cd build-newlib
../newlib-$NEWLIB_VER/configure --target=$TARGET --prefix="$PREFIX" --disable-multilib --disable-newlib-supplied-syscalls --enable-newlib-nano-malloc --enable-newlib-nano-formatted-io
make -j$JOBS; make install; cd "$WORK"
log "4/4 gcc full"; mkdir build-gcc2; cd build-gcc2
../gcc-$GCC_VER/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c --with-newlib --disable-multilib
make -j$JOBS; make install; cd "$WORK"
log verify
"$PREFIX/bin/$TARGET-gcc" --version | head -1
"$PREFIX/bin/$TARGET-ar" t "$PREFIX/$TARGET/lib/libc.a" | grep -qx libc_a-nano-vfprintf.o
"$PREFIX/bin/$TARGET-nm" "$PREFIX/$TARGET/lib/libc.a" 2>/dev/null > "$WORK/syms"; grep -qE ' T _printf_i$' "$WORK/syms"; grep -qE ' [BDC] __malloc_free_list$' "$WORK/syms"
log done
