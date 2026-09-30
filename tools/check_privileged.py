#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_privileged.py — ユーザランドの特権命令検査 (リング3 準備)

v2 でユーザプログラムは CPL=3 で走る (docs/archive/kernel_v2/M1_RING3.md)。CPL=3 では
特権命令・IOPL 依存命令は #GP になる。ビルド済みの userland/*.o を逆アセンブル
(既存の i386-elf-objdump) して、そういう命令が残っていないか検査する。

2 モード:
  (既定)     警告のみ。offender を [--] で列挙し exit 0。
             現在 userland は CPL=0 で動くので、これらは今は正常。
             make check はこのモードで呼ぶ (green ビルドを壊さない)。
  --strict   1 つでもあれば exit 1。リング3 が入ったあとのゲート用。
             os32-cycle / M1e 検証がこちらを使う。

「車輪の再発明」ではない: 逆アセンブラは objdump をそのまま使い、本スクリプトは
ニーモニック列の抽出とフィルタだけを行う (docs/tasks/v2 の方針)。
"""

import os
import re
import subprocess
import sys

PROJ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 検査対象。Rust の target/ (ビルド中間物) は除外する。
SCAN_DIRS = ["userland"]

# CPL=3 で #GP する命令 (ニーモニック完全一致)。
#   IOPL 依存: cli/sti (IF 操作), in/out 系 (ポート I/O)
#   特権: hlt, lgdt/lidt/lldt/ltr/lmsw, clts, invd/wbinvd/invlpg,
#         rdmsr/wrmsr, rdpmc(条件付き)
# mov cr/dr は別途オペランドで判定する (下の CR_DR_RE)。
PRIVILEGED = {
    "cli", "sti", "hlt", "clts",
    "in", "ins", "insb", "insw", "insl",
    "out", "outs", "outsb", "outsw", "outsl",
    "lgdt", "lidt", "lldt", "ltr", "lmsw", "sgdt", "sidt",
    "invd", "wbinvd", "invlpg", "rdmsr", "wrmsr",
    "iret", "iretd",  # ユーザが直接使うことはまず無いが CPL=3 では意味が違う
}
# ユーザが正当に使ってよく、名前が紛らわしいもの (除外を明示):
#   inc/int/into/inb?(=in の別表記は上で拾う)/invlpg は上で扱う
CR_DR_RE = re.compile(r"%(cr|dr)[0-9]")

# objdump 逆アセンブル行: "   3:\t57 89 e5   \tmov    %esp,%ebp"
#   アドレス:\t バイト列 \t ニーモニック オペランド
LINE_RE = re.compile(r"^\s*[0-9a-f]+:\t[0-9a-f ]+\t\s*([a-z][a-z0-9.]*)\s*(.*)$")


def objdump_bin():
    cross = os.environ.get("CROSS_DIR")
    if not cross:
        for env_file in (".env", ".env.sample"):
            path = os.path.join(PROJ_DIR, env_file)
            if os.path.isfile(path):
                for line in open(path):
                    line = line.strip()
                    if line.startswith("CROSS_DIR="):
                        cross = line.split("=", 1)[1].strip()
                        break
            if cross:
                break
    for cand in (
        os.path.join(cross, "bin", "i386-elf-objdump") if cross else None,
        "i386-elf-objdump",
    ):
        if not cand:
            continue
        try:
            subprocess.run([cand, "--version"], capture_output=True, check=True)
            return cand
        except (OSError, subprocess.CalledProcessError):
            continue
    return None


def scan_object(objdump, path):
    """(mnemonic, count) のリストを返す。"""
    try:
        out = subprocess.run([objdump, "-d", path], capture_output=True,
                             timeout=30).stdout.decode("utf-8", "replace")
    except (OSError, subprocess.TimeoutExpired):
        return []
    hits = {}
    for line in out.splitlines():
        m = LINE_RE.match(line)
        if not m:
            continue
        mnem, ops = m.group(1), m.group(2)
        bad = mnem in PRIVILEGED or (mnem == "mov" and CR_DR_RE.search(ops))
        if bad:
            key = mnem if mnem != "mov" else "mov cr/dr"
            hits[key] = hits.get(key, 0) + 1
    return sorted(hits.items())


def find_objects():
    objs = []
    for d in SCAN_DIRS:
        base = os.path.join(PROJ_DIR, d)
        for root, _, files in os.walk(base):
            if "target" in root.split(os.sep):   # Rust 中間物を除外
                continue
            for f in files:
                if f.endswith(".o"):
                    objs.append(os.path.join(root, f))
    return sorted(objs)


def main():
    strict = "--strict" in sys.argv
    objdump = objdump_bin()
    if not objdump:
        print("Error: i386-elf-objdump が見つからない (CROSS_DIR を確認)",
              file=sys.stderr)
        return 2

    objs = find_objects()
    if not objs:
        print("ユーザランドの .o が無い。先に make programs を実行すること。",
              file=sys.stderr)
        return 2

    findings = []
    for obj in objs:
        hits = scan_object(objdump, obj)
        if hits:
            findings.append((os.path.relpath(obj, PROJ_DIR), hits))

    print("== ユーザランドの特権命令 (CPL=3 で #GP) ==")
    if not findings:
        print("  なし")
        return 0

    mark = "[NG]" if strict else "[--]"
    for rel, hits in findings:
        detail = ", ".join("%s x%d" % (m, c) for m, c in hits)
        print("  %s %-40s %s" % (mark, rel, detail))

    if strict:
        print("")
        print("  リング3 では上記が #GP する。KAPI 経由 (sys_halt 等) に置換すること。")
        return 1

    print("")
    print("  (警告のみ。現在 userland は CPL=0 で動作。リング3 導入時に")
    print("   --strict でゲートする。sys_halt KAPI 等への置換が必要)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
