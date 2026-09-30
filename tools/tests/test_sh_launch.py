"""T9-S: sh.bin の起動待ちを実物の userland/shell/sh_launch.inc で確かめる。

票:   docs/archive/gui_v13/TASK_T9_sh.md §1 D3a (S 側 = シェル)
記録: tools/tests/t9_tdd.md

test_launch.py (K 側) と同じ様式 — ホスト ILP32 GNU11 で走らせたあと、
同じソースが外部プログラムと同じ形の i386-elf-gcc -Werror でも通ることを
別に見る ([C1] GNU11)。Make・エミュレータは使わない。

ホスト側は tools/tests/sh_launch_host.c が sh_launch.inc をそのまま
#include する (模型ではない)。差し替えるのは KernelAPI の 4 本
(launch_req / launch_poll / sys_yield / kprintf) だけで、DONE / FAILED /
STALE / FULL の 4 経路と「待ちの間 kbd_* / ime_* を呼ばない」を見る。
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
         "-fno-pie", "-fno-stack-protector", "-Wall", "-Wextra", "-Werror"]
INCLUDES = ["-I" + str(ROOT / "sdk/include/os32")]
HOST_SRC = ROOT / "tools/tests/sh_launch_host.c"
INC_SRC = ROOT / "userland/shell/sh_launch.inc"

# .inc は単体ではコンパイルできないので、ターゲット側の -Werror 確認は
# g_api だけを足した最小の翻訳単位で行う (libc は使わない)。
# sh_refuse は main.c (shell.h) 側の口。票 TASK_SH_TRUNCATION §5 の段 3 で
# sh_launch が T11 の断りに使うようになったので、宣言だけ足す。
TARGET_STUB = """#include "os32api.h"
KernelAPI *g_api;
void sh_refuse(const char *what, int limit);
#include "%s"
""" % INC_SRC


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-sh-launch-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "sh_launch"
        subprocess.run(["gcc", *FLAGS, "-O0", *INCLUDES,
                        "-nostdlib", "-static", "-no-pie",
                        str(HOST_SRC), "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST ILP32 GNU11 COMPILE PASS", flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=30).returncode

        stub = tmp / "sh_launch_target.c"
        stub.write_text(TARGET_STUB)
        subprocess.run(["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
                        "-ffreestanding", "-fno-pie", "-fno-stack-protector",
                        "-nostdlib", "-mno-red-zone", "-fcommon",
                        "-O2", "-Wall", "-Wextra", "-Werror",
                        "-D__OS32_USERLAND__", *INCLUDES, "-c", str(stub),
                        "-o", str(tmp / "sh_launch_target.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)
        print("EXIT sh_launch_host=%d" % rc, flush=True)
        sys.exit(rc)
