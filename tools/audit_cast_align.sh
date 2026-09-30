#!/bin/bash
# ============================================================================
#  audit_cast_align.sh — 非整列アクセス候補の洗い出し (他アーキテクチャ移植 M0)
#
#  GCC の -Wcast-align=strict は「アラインメント要件を上げるポインタキャスト」を
#  ターゲット非依存で警告する。x86 では黙って通るが ARMv5 などでは壊れる箇所を
#  機械的に列挙するために使う。
#
#  警告が出た = 必ず壊れる、ではない。オフセットが型サイズの倍数で、かつ
#  基底バッファが整列していれば安全である。仕分けの手順と 2026-09-08 時点の
#  結果は docs/tasks/arch_port/M0_PORTABILITY_AUDIT.md を参照。
#
#  使い方:  tools/audit_cast_align.sh [kernel|user|all]   (既定: all)
#
#  ホストの gcc -m32 を使う (i386-elf クロスコンパイラは不要)。
#  -fsyntax-only なので成果物は作らない。
# ============================================================================
set -u
cd "$(dirname "$0")/.." || exit 1

WHICH="${1:-all}"
COMMON="-I. -Iinclude -Isdk/include -Isdk/include/os32"
FLAGS="-std=gnu89 -m32 -ffreestanding -fno-pie -fno-stack-protector -fsyntax-only -Wcast-align=strict"

hits=0; ok=0; ng=0

sweep() {  # $1=インクルード列, $2..=ソース
    local incs="$1"; shift
    local f
    for f in "$@"; do
        [ -e "$f" ] || continue
        local out
        out=$(gcc $FLAGS $incs "$f" 2>&1)
        if [ $? -eq 0 ]; then ok=$((ok + 1)); else ng=$((ng + 1)); fi
        # 解析が途中で失敗したファイルでも、そこまでの警告は有効なので拾う
        local w
        w=$(printf '%s\n' "$out" | grep "cast increases required alignment")
        if [ -n "$w" ]; then
            printf '%s\n' "$w"
            hits=$((hits + $(printf '%s\n' "$w" | wc -l)))
        fi
    done
}

if [ "$WHICH" = kernel ] || [ "$WHICH" = all ]; then
    sweep "$COMMON -Ikernel -Idrivers -Inet -Ifs -Iexec -Igfx -Ilib -Ikapi -Ilib/sqlite3" kernel/*.c
    sweep "$COMMON -Idrivers -Igfx -Ilib"                                                 drivers/*.c
    sweep "$COMMON -Igfx -Idrivers -Ifs -Ilib -Ikernel"                                   gfx/*.c
    sweep "$COMMON -Ifs -Ifs/fatfs -Idrivers -Ikernel -Ilib"                              fs/*.c
    sweep "-Ifs/fatfs $COMMON -Ifs -Idrivers -Ikernel -Ilib"                              fs/fatfs/*.c
    sweep "$COMMON -Iexec -Ikapi -Ifs -Igfx -Idrivers -Ilib -Ikernel"                     exec/*.c
    sweep "$COMMON -Ikapi -Ikernel -Idrivers -Ifs -Iexec -Igfx -Ilib -Ilib/sqlite3"       kapi/*.c
    sweep "$COMMON -Ilib"                                                                 lib/*.c
fi

if [ "$WHICH" = user ] || [ "$WHICH" = all ]; then
    # userland は newlib ヘッダ (i386-elf クロス) を要するため解析失敗が多い。
    # 「警告 0 件」を網羅の根拠にしないこと — 下の解析成功数で判断する。
    for f in $(find userland -name '*.c' -not -path '*/rust/*' | sort); do
        sweep "$COMMON -Iuserland/lib -I$(dirname "$f")" "$f"
    done
fi

echo "----------------------------------------------------------------"
echo "警告 $hits 件 / 解析成功 $ok ファイル・解析失敗 $ng ファイル"
echo "仕分け結果: docs/tasks/arch_port/M0_PORTABILITY_AUDIT.md"
