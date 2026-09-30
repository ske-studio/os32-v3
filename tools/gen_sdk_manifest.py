#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_sdk_manifest.py — 配布 SDK の収録物一覧を作る

SDK は「OS のソースを持たない人がアプリをビルドするための契約」なので、
何が入っていて何が入っていないかが分かる必要がある。

出力する SDK_MANIFEST には:
  - KAPI バージョン (このSDKでビルドしたアプリが要求する最低カーネル版)
  - 収録ファイルの一覧・サイズ・SHA-256
  - 提供ライブラリと、リンク時に指定する -l 名

使い方: python3 tools/gen_sdk_manifest.py <SDKディレクトリ>
"""

import hashlib
import os
import sys


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    root = sys.argv[1].rstrip("/")

    with open(os.path.join(root, "KAPI_VERSION"), encoding="utf-8") as f:
        kapi = f.read().strip()

    entries = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            if name == "SDK_MANIFEST":
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            entries.append((rel, os.path.getsize(full), sha256(full)))
    entries.sort()

    libs = sorted(os.path.splitext(e[0].split("/")[-1])[0][3:]
                  for e in entries
                  if e[0].startswith("lib/") and e[0].endswith(".a"))

    out = []
    out.append("OS32 SDK")
    out.append("=" * 60)
    out.append("")
    out.append("KAPI_VERSION: {}".format(kapi))
    out.append("")
    out.append("このSDKでビルドしたプログラムは、KernelAPI v{} 以上の OS32 で".format(kapi))
    out.append("動作する。app.conf の最低APIバージョンはこれ以下にすること。")
    out.append("")
    out.append("ビルド方法:")
    out.append("  CC=i386-elf-gcc  AS=nasm  LD=i386-elf-ld")
    out.append("  -I<SDK>/include -I<SDK>/include/os32")
    out.append("  -T <SDK>/link/app.ld  -L<SDK>/lib")
    out.append("  <SDK>/crt/crt0.o crt0_c.o syscalls.o help.o を先頭にリンク")
    out.append("  最後に python3 <SDK>/bin/mkos32x.py で OS32X ヘッダを付ける")
    out.append("")
    out.append("提供ライブラリ ({} 本):".format(len(libs)))
    for i in range(0, len(libs), 4):
        out.append("  " + "  ".join("-l{}".format(l) for l in libs[i:i + 4]))
    out.append("")
    out.append("収録ファイル ({} 件):".format(len(entries)))
    out.append("-" * 60)
    for rel, size, digest in entries:
        out.append("{}  {:>9}  {}".format(digest[:16], size, rel))

    path = os.path.join(root, "SDK_MANIFEST")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("  SDK_MANIFEST: {} ファイル / ライブラリ {} 本 / KAPI v{}".format(
        len(entries), len(libs), kapi))
    return 0


if __name__ == "__main__":
    sys.exit(main())
