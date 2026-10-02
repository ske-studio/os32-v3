"""K7-K: 打鍵の注入リングを実物の kernel/kbd_inject.c で確かめる。

票:   docs/archive/gui_v13/TASK_K7_input.md §1 D2〜D5 / §5 (R1 / R2 / B)
記録: tools/tests/k7_tdd.md

test_con_sink.py と同じ様式 — ホスト ILP32 GNU11 で走らせたあと、同じ
ソースがカーネルと同じフラグの i386-elf-gcc -Werror でも通ることを別に
見る ([C1] GNU11)。Make・エミュレータ・libc は使わない。

kbd_inject_host.c は kernel/con_sink.c も同じ翻訳単位へ入れる: 注入の権限は
「con_sink の読み手 1 本」なので、そこを模型に置き換えると試験の意味が消える。

ホスト側だけ -DCON_SINK_NO_IRQ_LOCK / -DKBD_INJECT_NO_IRQ_LOCK を付ける:
CPL=3 では cli/popfl を実行できないため。クロス側は付けないので、
include/io.h を使う本番の経路も同じ試験の中でコンパイルされる。
"""
import host32
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror"]
# arch/x86 + platform/pc98: include/io.h は契約だけで、実装は固定名
# arch_io.h / platform_io.h を引く (順序 3)。build/config.mk の INC_COMMON と
# 同じものをここでも渡す。
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "arch/x86", "platform/pc98",
                      "kernel", "lib", "sdk/include/os32")]
HOST_SRC = ROOT / "tools/tests/kbd_inject_host.c"
KERNEL_SRC = ROOT / "kernel/kbd_inject.c"

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-kbd-inject-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "kbd-inject"
        subprocess.run(["gcc", *FLAGS, "-O0",
                        "-DCON_SINK_NO_IRQ_LOCK", "-DKBD_INJECT_NO_IRQ_LOCK",
                        "-D__KERNEL_BUILD__", *INCLUDES,
                        "-nostdlib", "-static", "-no-pie",
                        str(HOST_SRC), "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST ILP32 GNU11 COMPILE PASS", flush=True)
        rc = host32.run([str(exe)], cwd=ROOT, timeout=30).returncode
        # 本番の割込み禁止区間 (include/io.h) を含む形でクロスコンパイル
        subprocess.run(["i386-elf-gcc", *FLAGS, "-D__KERNEL_BUILD__",
                        *INCLUDES, "-O2", "-c", str(KERNEL_SRC),
                        "-o", str(tmp / "kbd_inject.o")], cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)
        print("EXIT kbd_inject_host=%d" % rc, flush=True)
        sys.exit(rc)
