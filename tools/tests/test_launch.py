"""T9-K: 起動要求表を実物の exec/launch.c で確かめる。

票:   docs/archive/gui_v13/TASK_T9_sh.md §1 D3 / §1a (K 側 = カーネル + KAPI v49)
記録: tools/tests/t9_tdd.md

test_con_sink.py と同じ様式 — ホスト ILP32 GNU11 で走らせたあと、同じソースが
カーネルと同じフラグの i386-elf-gcc -Werror でも通ることを別に見る
([C1] GNU11)。Make・エミュレータ・libc は使わない。

ホスト側は exec/appslot.c と exec/launch.c を tools/tests/launch_host.c が
そのまま #include する (模型ではない)。カーネル帯の代わりに要るのは所有者
(res_owner_get/set)・GUI 判定 (con_sink_is_enabled)・kstrncpy の 3 つだけ。
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror"]
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "kernel", "lib", "exec", "fs", "sdk/include/os32")]
HOST_SRC = ROOT / "tools/tests/launch_host.c"
KERNEL_SRC = ROOT / "exec/launch.c"

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-launch-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "launch"
        subprocess.run(["gcc", *FLAGS, "-O0", "-D__KERNEL_BUILD__", *INCLUDES,
                        "-nostdlib", "-static", "-no-pie",
                        str(HOST_SRC), "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST ILP32 GNU11 COMPILE PASS", flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=30).returncode
        subprocess.run(["i386-elf-gcc", *FLAGS, "-D__KERNEL_BUILD__",
                        *INCLUDES, "-O2", "-c", str(KERNEL_SRC),
                        "-o", str(tmp / "launch.o")], cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)
        print("EXIT launch_host=%d" % rc, flush=True)
        sys.exit(rc)
