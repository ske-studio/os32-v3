"""ext2_read_file() が max_size を越えて書かないことの回帰試験。

K5b-K の実機初回起動 (2026-09-11) で「FATAL: shell.bin load failed」を出した
根本原因の試験。ext2_read_block() は常に 1KB 書くので、端数ブロックを宛先へ
直接読むと最大 1023 バイト溢れる。K5b-K が exec のヘッダ先読みを 108 バイトの
カーネル .bss バッファへ変えたことで、隣の resolved[] (解決済みパス) が潰れ、
続く本体読み込みが NOT_FOUND を返していた。

実物の fs/ext2/ext2_file.c をそのまま取り込み、ブロック I/O・inode・bmap だけを
差し替える。実デバイス・実イメージ・エミュレータには一切触れない。
記録: tools/tests/k5b_kernel_tdd.md 節 R
"""
from kernel_includes import include_flags
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["bound_header", "bound_tail", "bound_aligned", "exec_bss_neighbour",
         "hole_and_errors", "stream_ranges"]
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", "-Wno-sign-compare",
         "-D__cdecl="]
INCLUDES = include_flags(ROOT)
SRC = ROOT / "tools/tests/ext2_read_bound_host.c"

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-ext2-bound-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "ext2-read-bound"
        subprocess.run(["gcc", *FLAGS, *INCLUDES, str(SRC), "-o", str(exe)],
                       cwd=ROOT, check=True)
        print("HOST GNU11 -Werror COMPILE PASS", flush=True)
        failed = 0
        mutate = "--mutate" in sys.argv[1:]
        cases = [arg for arg in sys.argv[1:] if arg != "--mutate"] or CASES
        for case in cases:
            rc = subprocess.run([str(exe), case], cwd=ROOT).returncode
            print(f"EXIT {case}={rc}", flush=True)
            failed += rc != 0
        if mutate:
            file_source = (ROOT / "fs/ext2/ext2_file.c").read_text()
            run_source = (ROOT / "fs/ext2/ext2_file_read_run.inc").read_text()
            # hole/error の目印は両入口が使う ext2_read_inode_range 内。
            mutants = [
                ("stream-type", file_source, "if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) return EXT2_ERR_ISDIR;\n\n    if (offset >= inode.size)",
                 "if (0) return EXT2_ERR_ISDIR;\n\n    if (offset >= inode.size)", "stream_ranges"),
                ("stream-hole", run_source, "if (phys == 0) break;\n\n        to_copy = EXT2_BLOCK_SIZE - byte_in_blk;",
                 "if (phys == 0) return EXT2_ERR_IO;\n\n        to_copy = EXT2_BLOCK_SIZE - byte_in_blk;", "hole_and_errors"),
                ("file-error", run_source, "if (ret != 0) return EXT2_ERR_IO;\n                if (next == 0",
                 "if (ret != 0) return (int)total_read;\n                if (next == 0", "hole_and_errors"),
            ]
            for name, source, old, new, case in mutants:
                if source.count(old) != 1:
                    raise RuntimeError(f"{name}: mutation marker occurs {source.count(old)} times")
                mutated = tmp / "ext2_file_mutated.c"
                if source == run_source:
                    run_copy = tmp / "ext2_file_read_run.inc"
                    run_copy.write_text(source.replace(old, new))
                    mutated.write_text(file_source.replace('#include "ext2_file_read_run.inc"',
                                                          f'#include "{run_copy}"'))
                else:
                    mutated.write_text(source.replace(old, new))
                fixture = tmp / "fixture.c"
                fixture.write_text(SRC.read_text().replace(
                    '#include "../../fs/ext2/ext2_file.c"',
                    f'#include "{mutated}"'))
                mutant_exe = tmp / "mutant"
                subprocess.run(["gcc", *FLAGS, *INCLUDES, str(fixture), "-o", str(mutant_exe)],
                               cwd=ROOT, check=True)
                rc = subprocess.run([str(mutant_exe), case], cwd=ROOT).returncode
                print(f"MUTANT {name} runtime rc={rc}", flush=True)
                if rc == 0:
                    failed += 1
        # 実物と同じフラグでクロスコンパイルも通ること ([C1] GNU11)
        subprocess.run(["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
                        "-ffreestanding", "-fno-pie", "-fno-stack-protector",
                        "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-Wno-sign-compare",
                        *INCLUDES, "-O2", "-c", str(ROOT / "fs/ext2/ext2_file.c"),
                        "-o", str(tmp / "ext2_file.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)
        print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
