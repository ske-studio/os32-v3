"""T9-S: sh.bin の行再描画とスクリプトの exit を実物のソースで確かめる。

票:   docs/archive/gui_v13/TASK_T9_sh.md §1 D2(d)、実装レビュー (往復 1/3) の
      blocker 1 (TAB 補完後の redraw が座標に依存) / blocker 2 (source 中の
      exit が後続を止めない)
記録: tools/tests/t9_tdd.md

test_launch.py / test_sh_launch.py と同じ様式 — ホスト ILP32 GNU11 で走らせ、
同じソースが外部プログラムと同じ形の i386-elf-gcc -Werror でも通ることを
別に見る ([C1] GNU11)。Make・エミュレータは使わない。

ホスト側は tools/tests/sh_shell_host.c が userland/shell/sh_redraw.inc と
userland/shell/cmd_script.c をそのまま #include する (模型ではない)。
libc は使わない (-nostdlib) ので、shell.h が引く <string.h> だけ一時
ディレクトリに薄いシムを置く (test_owner_reclaim.py と同じやり方)。
"""
import host32
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASE = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
        "-fno-stack-protector", "-Wall", 
        "-D__OS32_USERLAND__", "-DSHELL_AS_APP"]
INCLUDES = ["-I" + str(ROOT / "sdk/include/os32"), "-I" + str(ROOT / "include"),
            "-I" + str(ROOT / "userland/shell")]
HOST_SRC = ROOT / "tools/tests/sh_shell_host.c"

STRING_SHIM = """/* テスト用の薄い <string.h>。実体は sh_shell_host.c にある。 */
#ifndef OS32_TEST_STRING_H
#define OS32_TEST_STRING_H
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, unsigned long n);
unsigned long strlen(const char *s);
void *memcpy(void *d, const void *s, unsigned long n);
char *strncpy(char *d, const char *s, unsigned long n);
char *strncat(char *d, const char *s, unsigned long n);
#endif
"""

STDIO_SHIM = """/* テスト用の薄い <stdio.h>。cmd_file.c / cmd_mnt.c が使うのは printf だけ。 */
#ifndef OS32_TEST_STDIO_H
#define OS32_TEST_STDIO_H
int printf(const char *fmt, ...);
#endif
"""

STDLIB_SHIM = """/* テスト用の薄い <stdlib.h>。cmd_mnt.c が使うのは atoi だけ。 */
#ifndef OS32_TEST_STDLIB_H
#define OS32_TEST_STDLIB_H
int atoi(const char *s);
#endif
"""


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-sh-shell-") as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "string.h").write_text(STRING_SHIM)
        (tmp / "stdio.h").write_text(STDIO_SHIM)
        (tmp / "stdlib.h").write_text(STDLIB_SHIM)
        shim = ["-I" + str(tmp)]
        exe = tmp / "sh_shell"
        subprocess.run(["gcc", *BASE, "-O0", *shim, *INCLUDES,
                        "-nostdlib", "-static", "-no-pie",
                        str(HOST_SRC), "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST ILP32 GNU11 COMPILE PASS", flush=True)
        rc = host32.run([str(exe)], cwd=ROOT, timeout=30).returncode

        subprocess.run(["i386-elf-gcc", *BASE, "-O2", "-nostdlib",
                        "-mno-red-zone", "-fcommon", *shim, *INCLUDES,
                        "-c", str(HOST_SRC), "-o", str(tmp / "sh_shell.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 COMPILE PASS", flush=True)
        print("EXIT sh_shell_host=%d" % rc, flush=True)
        sys.exit(rc)
