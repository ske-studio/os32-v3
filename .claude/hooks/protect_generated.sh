#!/bin/bash
# PreToolUse (Edit/Write/MultiEdit): KAPI の生成物の手編集を拒否する ([ABI1])。
# 対象は「ファイル全体が sdk/gen_kapi.py / kapi_rust_gen.py の出力」のものだけ。
# 生成区間を持つ手書きのリンカスクリプト (build/os32.ld、sdk/link/app.ld・app_sys.ld・shlib.ld) は対象外
# — 区間の中は make check-kapi-* が照合する。生成は sdk/kapi.json を直して make で。
GENERATED='sdk/include/os32/os32_kapi_generated.h
sdk/include/os32/os32_kapi_slots.h
kapi/kapi_generated.c
exec/exec_kapi_init.inc
sdk/include/os32/os32_generations.h
sdk/os32_generations.py
sdk/rust/os32api/src/generations.rs
sdk/include/os32/os32_unit_stamp.h
sdk/crt/generations.inc
sdk/link/generations.ld
sdk/rust/os32api/src/kapi_generated.rs'
input=$(cat)
path=$(printf '%s' "$input" | sed -n 's/.*"file_path"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1)
[ -n "$path" ] || exit 0
path=$(realpath -m "$path" 2>/dev/null || printf '%s' "$path")
dir=$(dirname "$path"); while [ ! -d "$dir" ]; do dir=$(dirname "$dir"); done
root=$(git -C "$dir" rev-parse --show-toplevel 2>/dev/null) || exit 0
root=$(realpath -m "$root")
rel=${path#"$root"/}
if printf '%s\n' "$GENERATED" | grep -qxF "$rel"; then
    echo "[ABI1] $rel は sdk/kapi.json から生成される — 手で直さず kapi.json を直して make で再生成する" >&2
    exit 2
fi
exit 0
