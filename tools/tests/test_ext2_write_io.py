"""ext2 の 1 回の書き込みが起こす 512B セクタ I/O の回数を数える (票 S6-P)。

`tar c /tmp/e8.tar /etc/system.cfg` が ext2 (hd0) で 15 秒を超える件の診断と
回帰。実物の fs/ext2_*.c をそのまま取り込み、Device API と IDE だけを贋物に
差し替えて、RAM 上の 8MB ディスクを実物の ext2_format() で作る。実デバイス・
実イメージ・エミュレータには一切触れない。

記録: tools/tests/s6p_tdd.md
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["w512_new", "w512_existing", "w1_append", "seek_back",
         "tar_sequence", "read512", "write_through", "ns_invalidation",
         "sync_on_alloc", "meta_reaches_disk"]
FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-nostdlib", "-static", "-O1",
         "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", "-Wno-sign-compare",
         "-Wdeclaration-after-statement", "-D__cdecl=", "-DMTAR_NO_STDIO"]
# mtar_freestanding は必ず先頭 — 32bit の glibc ヘッダが無い環境で
# lib/microtar/microtar.c の <stdio.h> / <stdlib.h> / <string.h> を埋める。
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("tools/tests/mtar_freestanding",
                      "include", "fs", "lib", "kernel", "drivers",
                      "lib/microtar", "sdk/include/os32")]
SRC = ROOT / "tools/tests/ext2_write_io_host.c"
TARGET_SRCS = ["fs/ext2_super.c", "fs/ext2_inode.c", "fs/ext2_dir.c",
               "fs/ext2_file.c", "fs/ext2_vfs.c"]

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-ext2-wio-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "ext2-write-io"
        subprocess.run(["gcc", *FLAGS, *INCLUDES, str(SRC), "-o", str(exe)],
                       cwd=ROOT, check=True)
        print("HOST GNU89 -Werror COMPILE PASS", flush=True)
        failed = 0
        cases = sys.argv[1:] or CASES
        for case in cases:
            rc = subprocess.run([str(exe), case], cwd=ROOT).returncode
            print(f"EXIT {case}={rc}", flush=True)
            failed += rc != 0
        # 実物と同じフラグでクロスコンパイルも通ること ([C1] C89/GNU89)
        for src in TARGET_SRCS:
            subprocess.run(["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
                            "-ffreestanding", "-fno-pie", "-fno-stack-protector",
                            "-Wall", "-Wextra", "-Werror",
                            "-Wdeclaration-after-statement",
                            "-Wno-unused-parameter", "-Wno-sign-compare",
                            *INCLUDES, "-O2", "-c", str(ROOT / src),
                            "-o", str(tmp / (pathlib.Path(src).stem + ".o"))],
                           cwd=ROOT, check=True)
        print("TARGET i386-elf GNU89 -Werror COMPILE PASS", flush=True)
        print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
