"""ext2_read_file() が max_size を越えて書かないことの回帰試験。

K5b-K の実機初回起動 (2026-09-11) で「FATAL: shell.bin load failed」を出した
根本原因の試験。ext2_read_block() は常に 1KB 書くので、端数ブロックを宛先へ
直接読むと最大 1023 バイト溢れる。K5b-K が exec のヘッダ先読みを 108 バイトの
カーネル .bss バッファへ変えたことで、隣の resolved[] (解決済みパス) が潰れ、
続く本体読み込みが NOT_FOUND を返していた。

実物の fs/ext2_file.c をそのまま取り込み、ブロック I/O・inode・bmap だけを
差し替える。実デバイス・実イメージ・エミュレータには一切触れない。
記録: tools/tests/k5b_kernel_tdd.md 節 R
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["bound_header", "bound_tail", "bound_aligned", "exec_bss_neighbour"]
FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", "-Wno-sign-compare",
         "-Wdeclaration-after-statement", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "fs", "lib", "kernel", "drivers",
                      "sdk/include/os32")]
SRC = ROOT / "tools/tests/ext2_read_bound_host.c"

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-ext2-bound-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "ext2-read-bound"
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
        subprocess.run(["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
                        "-ffreestanding", "-fno-pie", "-fno-stack-protector",
                        "-Wall", "-Wextra", "-Werror",
                        "-Wdeclaration-after-statement",
                        "-Wno-unused-parameter", "-Wno-sign-compare",
                        *INCLUDES, "-O2", "-c", str(ROOT / "fs/ext2_file.c"),
                        "-o", str(tmp / "ext2_file.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU89 -Werror COMPILE PASS", flush=True)
        print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
