#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_le_access.py — 外部形式 (LE) の直アクセス検査 (移植性の準備、順序 4-a)

媒体の上に並ぶバイト列 (ext2 の superblock / group descriptor / inode /
ディレクトリエントリ / 間接表、ISO9660 のレコード、KCG フォント書庫のヘッダ)
を `*(u32 *)&buf[off]` で読み書きすると、2 つの前提が同時に隠れる:

  1. CPU がリトルエンディアンであること
  2. 非アラインアクセスが許されること

x86 ではどちらも成り立つので動いてしまうが、ARM (SCTLR.A=1) では 2 で落ち、
ビッグエンディアンの CPU では 1 で値が化ける。外部形式は
`include/endian_le.h` の `le16_rd` / `le16_wr` / `le32_rd` / `le32_wr`
(バイト単位で組み立てる純 C) を通すこと。

## 2 つの検査

**(1) 文字列検査** — 対象ファイルに `*(uNN *)&` の形が無いこと。
コンパイラが要らないので、どの環境でも必ず走る。

**(2) コンパイル検査** — 対象ファイルを `-Wcast-align=strict` で単体
コンパイル (`-fsyntax-only`) し、警告が 0 であること。文字列検査をすり抜ける
書き方 (`u32 *p = (u32 *)buf;` と 2 行に分ける等) はこちらが捕まえる。

`-Wcast-align` の既定は「非アラインアクセスを許す CPU では黙る」ので、
x86 のホストで見張るには **`=strict` が要る**。フラグとインクルードは
`build/config.mk` を読んで実ビルドに揃える (写すと二重管理になる)。
読み方は tools/check_arm_compile.py と共有する。

コンパイラが見つからない環境では (2) を SKIP して (1) だけで通す ([V4] の
とおり、飛ばしたことは出力に書く)。

## 対象を増やすとき

新しく外部形式を触るファイルを書いたら GUARDED に足す。逆に、ここに無い
ファイルは検査されない — ext2 のビットマップのように 1 バイトずつ触る
コードは元から移植性があるので、足す必要はない。
"""

import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_arm_compile as gauge  # noqa: E402  (config.mk の読み方を借りる)

PROJ_DIR = gauge.PROJ_DIR

# 外部形式 (媒体・書庫) を読み書きするファイル。
GUARDED = [
    "fs/ext2_super.c",
    "fs/ext2_inode.c",
    "fs/ext2_dir.c",
    "fs/ext2_file.c",
    "fs/ext2_fmt.c",
    "fs/ext2_vfs.c",
    "fs/iso9660.c",
    "drivers/kcg.c",
    "lib/utf8.c",
]

# `*(u32 *)&...` / `*(const u16 *)&...` — キャスト経由の直アクセス。
DIRECT_RE = re.compile(r"\*\s*\(\s*(?:const\s+)?u(?:8|16|32|64)\s*\*\s*\)\s*&")

# 実ビルドのコンパイラ。無ければホストの gcc -m32 で代用する。
CC_CANDIDATES = [
    ("i386-elf-gcc", []),
    ("gcc", []),
]

ALIGN_FLAG = "-Wcast-align=strict"
DEPFLAGS_DROP = ["-MMD", "-MP"]


def which(name):
    for d in os.environ.get("PATH", "").split(os.pathsep):
        p = os.path.join(d, name)
        if os.access(p, os.X_OK) and not os.path.isdir(p):
            return p
    return None


def scan_text(rel):
    """(1) 文字列検査。返り値: [(行番号, 行), ...]"""
    path = os.path.join(PROJ_DIR, rel)
    hits = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f, 1):
            code = line.split("/*", 1)[0]     # 行内コメントの説明文は見逃す
            if DIRECT_RE.search(code):
                hits.append((i, line.rstrip()))
    return hits


def build_cmd(cc, flags, incmap, rel):
    inc = incmap[gauge.inc_var_for(rel)]
    return [cc] + flags + inc + [ALIGN_FLAG, "-fsyntax-only", rel]


def main():
    problems = []

    # ---- (1) 文字列検査 (コンパイラ不要) ----
    for rel in GUARDED:
        for (ln, line) in scan_text(rel):
            problems.append("%s:%d: 外部形式へのキャスト経由の直アクセス\n"
                            "         %s" % (rel, ln, line.strip()))

    # ---- (2) -Wcast-align=strict ----
    cc = None
    for name, _ in CC_CANDIDATES:
        cc = which(name)
        if cc:
            break

    warned = []
    if cc is None:
        print("=== 外部形式 (LE) の直アクセス検査 ===")
        print("  [SKIP] コンパイラが無いので -Wcast-align=strict は掛けていない")
    else:
        varmap = gauge.parse_make_vars(gauge.read(gauge.CONFIG_MK))
        flags = [f for f in gauge.expand_var(varmap, "CFLAGS_COMMON").split()
                 if f not in DEPFLAGS_DROP]
        flags += ["-O2", "-D__KERNEL_BUILD__"]
        incmap = {}
        for var in set(gauge.inc_var_for(r) for r in GUARDED):
            incmap[var] = gauge.expand_var(varmap, var).split()

        for rel in GUARDED:
            proc = subprocess.run(build_cmd(cc, flags, incmap, rel),
                                  cwd=PROJ_DIR, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT,
                                  universal_newlines=True)
            for line in proc.stdout.split("\n"):
                if "-Wcast-align" in line:
                    warned.append(line.strip())
            if proc.returncode != 0 and not warned:
                problems.append("%s: コンパイルできない (検査が成立しない)\n"
                                "         %s"
                                % (rel, gauge.first_error(proc.stdout)))
        for line in warned:
            problems.append("%s\n         (-Wcast-align=strict)" % line)

    if problems:
        print("=== 外部形式 (LE) の直アクセス検査: NG (%d 件) ===" % len(problems))
        for p in problems:
            print("  [NG] %s" % p)
        print("")
        print("  媒体・書庫の上のバイト列は include/endian_le.h を通すこと:")
        print("    le16_rd(p) / le32_rd(p)        LE で読む (番地は任意)")
        print("    le16_wr(p, v) / le32_wr(p, v)  LE で書く (番地は任意)")
        print("")
        print("  カーネル内のメモリ構造 (外部形式ではないもの) は対象外。")
        print("  その判断をしたなら、対象ファイルを tools/check_le_access.py の")
        print("  GUARDED から外すのではなく、コードの側をアクセサに寄せること。")
        return 1

    print("=== 外部形式 (LE) の直アクセス検査: OK (%d ファイル) ===" % len(GUARDED))
    if cc is not None:
        print("    %s %s — 警告 0" % (os.path.basename(cc), ALIGN_FLAG))
    return 0


if __name__ == "__main__":
    sys.exit(main())
