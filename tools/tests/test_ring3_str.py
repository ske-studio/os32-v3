"""T9-K R1: KAPI が CPL=3 へ返す文字列の置き場を実物の exec/ring3_str.c で確かめる。

票:   docs/archive/gui_v13/TASK_T9_sh.md §12 R1 (Codex 網羅レビュー 往復 7)
記録: tools/tests/t9_tdd.md

sys_getcwd は fs/vfs.c の static cwd (カーネル帯) をそのまま返していた。
カーネル帯の PTE には USER ビットが無い (kernel/paging.c が phys | PAGE_RW で
張る) ので、CPL=3 の sh.bin が `cd` / `pwd` で戻り値を読むと #PF → fault kill
になる。写し先は KAPI トランポリンページ (RO+USER) の、表とスタブの後ろの空き。

test_launch.py と同じ様式 — ホスト ILP32 GNU89 で走らせたあと、同じソースが
カーネルと同じフラグの i386-elf-gcc -Werror でも通ることを別に見る
([C1] C89/GNU89)。Make・エミュレータ・libc は使わない。
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement"]
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "kernel", "lib", "exec", "sdk/include/os32")]
HOST_SRC = ROOT / "tools/tests/ring3_str_host.c"
KERNEL_SRC = ROOT / "exec/ring3_str.c"

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-ring3-str-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "ring3-str"
        subprocess.run(["gcc", *FLAGS, "-O0", "-D__KERNEL_BUILD__", *INCLUDES,
                        "-nostdlib", "-static", "-no-pie",
                        str(HOST_SRC), "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST ILP32 GNU89 COMPILE PASS", flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=30).returncode
        subprocess.run(["i386-elf-gcc", *FLAGS, "-D__KERNEL_BUILD__",
                        *INCLUDES, "-O2", "-c", str(KERNEL_SRC),
                        "-o", str(tmp / "ring3_str.o")], cwd=ROOT, check=True)
        print("TARGET i386-elf GNU89 -Werror COMPILE PASS", flush=True)
        print("EXIT ring3_str_host=%d" % rc, flush=True)
        sys.exit(rc)
