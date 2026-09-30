#!/usr/bin/env bash
# i386-elf クロスツールチェーン (binutils 2.41 + GCC 13.2.0 + newlib 4.4.0 nano) を構築する。
#
# 手順の正典は docs/08_build.md §8-5。GitHub Actions (.github/workflows/build.yml, §8-6)
# と手元の再構築の両方で使う。build/config.mk が `i386-elf-*` の名前と
# `$(CROSS_DIR)/lib/gcc/i386-elf/13.2.0` をハードコードしているので版は固定。
#
# 使い方:
#   tools/ci/build_cross.sh [--prefix DIR] [--src-dir DIR] [--work DIR] [--jobs N]
#     --prefix   インストール先 (既定 $HOME/opt/cross)
#     --src-dir  tarball の置き場。無いものだけ公式 URL から curl で取る
#                (既定: --work の下)
#     --work     展開・ビルド用ディレクトリ (既定: mktemp -d、成功したら消す)
#     --jobs     make -j の値 (既定 nproc)
#
# このスクリプトの内容が変わると CI のキャッシュキー (hashFiles) も変わり、
# ツールチェーンが作り直される (約 30 分)。
set -euo pipefail

TARGET=i386-elf
BINUTILS_VER=2.41
GCC_VER=13.2.0
NEWLIB_VER=4.4.0.20231231

BINUTILS_TAR="binutils-${BINUTILS_VER}.tar.xz"
GCC_TAR="gcc-${GCC_VER}.tar.xz"
NEWLIB_TAR="newlib-${NEWLIB_VER}.tar.gz"

BINUTILS_SHA256=ae9a5789e23459e59606e6714723f2d3ffc31c03174191ef0d015bdf06007450
GCC_SHA256=e275e76442a6067341a27f04c5c6b83d8613144004c0413528863dc6b5c743da
NEWLIB_SHA256=0c166a39e1bf0951dfafcd68949fe0e4b6d3658081d6282f39aeefc6310f2f13

BINUTILS_URL="https://ftp.gnu.org/gnu/binutils/${BINUTILS_TAR}"
GCC_URL="https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/${GCC_TAR}"
NEWLIB_URL="https://sourceware.org/pub/newlib/${NEWLIB_TAR}"

PREFIX="$HOME/opt/cross"
SRC_DIR=""
WORK=""
JOBS="$(nproc)"

usage() {
    awk 'NR>1 && !/^#/ {exit} NR>1 {sub(/^# ?/, ""); print}' "$0"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)  PREFIX="$2"; shift 2 ;;
        --src-dir) SRC_DIR="$2"; shift 2 ;;
        --work)    WORK="$2"; shift 2 ;;
        --jobs)    JOBS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "build_cross.sh: 不明な引数: $1" >&2; usage >&2; exit 2 ;;
    esac
done

REMOVE_WORK=0
if [ -z "$WORK" ]; then
    WORK="$(mktemp -d "${TMPDIR:-/tmp}/os32-cross.XXXXXX")"
    REMOVE_WORK=1
fi
mkdir -p "$WORK" "$PREFIX"
WORK="$(cd "$WORK" && pwd)"
PREFIX="$(cd "$PREFIX" && pwd)"
[ -n "$SRC_DIR" ] || SRC_DIR="$WORK/dl"
mkdir -p "$SRC_DIR"
SRC_DIR="$(cd "$SRC_DIR" && pwd)"

export PATH="$PREFIX/bin:$PATH"

log() { echo "=== [build_cross $(date +%H:%M:%S)] $*"; }

# tarball を用意して sha256 を確かめる。手元に無いものだけ取得する。
fetch() {
    local tar="$1" url="$2" sum="$3"
    if [ ! -f "$SRC_DIR/$tar" ]; then
        log "取得: $url"
        curl -fL --retry 3 -o "$SRC_DIR/$tar.part" "$url"
        mv "$SRC_DIR/$tar.part" "$SRC_DIR/$tar"
    fi
    echo "$sum  $SRC_DIR/$tar" | sha256sum -c -
}

fetch "$BINUTILS_TAR" "$BINUTILS_URL" "$BINUTILS_SHA256"
fetch "$GCC_TAR"      "$GCC_URL"      "$GCC_SHA256"
fetch "$NEWLIB_TAR"   "$NEWLIB_URL"   "$NEWLIB_SHA256"

log "展開 -> $WORK"
cd "$WORK"
rm -rf "binutils-$BINUTILS_VER" "gcc-$GCC_VER" "newlib-$NEWLIB_VER" \
       build-binutils build-gcc1 build-newlib build-gcc2
tar xf "$SRC_DIR/$BINUTILS_TAR"
tar xf "$SRC_DIR/$GCC_TAR"
tar xf "$SRC_DIR/$NEWLIB_TAR"

log "[1/4] binutils $BINUTILS_VER"
mkdir build-binutils && cd build-binutils
"../binutils-$BINUTILS_VER/configure" --target="$TARGET" --prefix="$PREFIX" \
    --with-sysroot --disable-nls --disable-werror
make -j"$JOBS"
make install
cd "$WORK"

log "[2/4] gcc $GCC_VER stage1 (C のみ、ヘッダ無し、libgcc まで)"
mkdir build-gcc1 && cd build-gcc1
"../gcc-$GCC_VER/configure" --target="$TARGET" --prefix="$PREFIX" \
    --disable-nls --enable-languages=c --without-headers
make -j"$JOBS" all-gcc all-target-libgcc
make install-gcc install-target-libgcc
cd "$WORK"

# ★ nano 構成必須 ★ 通常構成だと printf 系がフル実装になり、各コマンド .bin が
# 約 3 倍に肥大して 1.2MB ブート FD に収まらない (§8-5)。
log "[3/4] newlib $NEWLIB_VER (nano)"
mkdir build-newlib && cd build-newlib
"../newlib-$NEWLIB_VER/configure" --target="$TARGET" --prefix="$PREFIX" \
    --disable-multilib \
    --disable-newlib-supplied-syscalls \
    --enable-newlib-nano-malloc \
    --enable-newlib-nano-formatted-io
make -j"$JOBS"
make install
cd "$WORK"

log "[4/4] gcc $GCC_VER full (with newlib)"
mkdir build-gcc2 && cd build-gcc2
"../gcc-$GCC_VER/configure" --target="$TARGET" --prefix="$PREFIX" \
    --disable-nls --enable-languages=c --with-newlib
make -j"$JOBS"
make install
cd "$WORK"

# ---- 検証 ----
log "検証"
fail() { echo "build_cross.sh: 検証失敗: $*" >&2; exit 1; }

GCC_OUT="$("$PREFIX/bin/$TARGET-gcc" -dumpversion)"
[ "$GCC_OUT" = "$GCC_VER" ] || fail "$TARGET-gcc -dumpversion = $GCC_OUT (期待 $GCC_VER)"
[ -d "$PREFIX/lib/gcc/$TARGET/$GCC_VER" ] || fail "$PREFIX/lib/gcc/$TARGET/$GCC_VER が無い"
LIBC="$PREFIX/$TARGET/lib/libc.a"
[ -f "$LIBC" ] || fail "$LIBC が無い"
[ -f "$PREFIX/$TARGET/include/stdio.h" ] || fail "newlib のヘッダが無い"

# nano 構成の判定 (2026-09-23 に手元の 2 つの libc.a で確かめた):
#   nano-formatted-io: nano 版は libc.a に libc_a-nano-vfprintf.o というメンバーがあり、
#     整数変換の _printf_i を定義する。通常構成には libc_a-vfprintf.o しか無く、
#     _printf_i も無い。(_printf_float はどちらでも定義として出ないので判定に使えない。)
#   nano-malloc: nano 版の mallocr.o は __malloc_free_list を定義し、__malloc_av_ を持たない。
#     通常構成 (dlmalloc) はその逆。
# ⚠ `echo "$SYMS" | grep -q` は使わない: grep -q が最初の一致で抜けると echo が
#   SIGPIPE を受け、`set -o pipefail` でパイプライン全体が失敗になる (GitHub Actions の
#   初回 run 35819997848 が「_printf_i を定義していない」で落ちた。手元では運良く通っていた)。
#   ファイルに落として grep する。
NM="$PREFIX/bin/$TARGET-nm"
AR="$PREFIX/bin/$TARGET-ar"
MEMBERS="$WORK/libc.members"
SYMS="$WORK/libc.syms"
"$AR" t "$LIBC" > "$MEMBERS"
"$NM" "$LIBC" > "$SYMS" 2>/dev/null
grep -qx 'libc_a-nano-vfprintf.o' "$MEMBERS" \
    || fail "libc.a に libc_a-nano-vfprintf.o が無い (nano-formatted-io でない)"
grep -qE ' T _printf_i$' "$SYMS" \
    || fail "libc.a が _printf_i を定義していない (nano-formatted-io でない)"
grep -qE ' [BDC] __malloc_free_list$' "$SYMS" \
    || fail "libc.a に __malloc_free_list が無い (nano-malloc でない)"
if grep -qE ' [BDC] __malloc_av_$' "$SYMS"; then
    fail "libc.a に __malloc_av_ がある (通常構成の dlmalloc)"
fi

"$PREFIX/bin/$TARGET-gcc" --version | sed -n 1p
echo "prefix: $PREFIX ($(du -sh "$PREFIX" | cut -f1))"
echo "newlib: nano-formatted-io + nano-malloc を確認"

if [ "$REMOVE_WORK" = 1 ]; then
    cd /
    rm -rf "$WORK"
fi
log "完了"
