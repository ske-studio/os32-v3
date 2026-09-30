"""K6C: console シンクのリングを実物の kernel/con_sink.c で確かめる。

票:   docs/archive/gui_v13/TASK_K6C_console.md §2 (設計 3 点 + レコード形式)
記録: tools/tests/con_sink_tdd.md

test_multiapp_model.py と同じ様式 — ホスト ILP32 GNU11 で走らせたあと、
同じソースがカーネルと同じフラグの i386-elf-gcc -Werror でも通ることを
別に見る ([C1] GNU11)。Make・エミュレータ・libc は使わない。

ホスト側だけ -DCON_SINK_NO_IRQ_LOCK を付ける: CPL=3 では cli/popfl を
実行できないため。クロス側は付けないので、include/io.h を使う本番の経路も
同じ試験の中でコンパイルされる。
"""
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
                      "kernel", "lib", "drivers", "sdk/include/os32")]
HOST_SRC = ROOT / "tools/tests/con_sink_host.c"
KERNEL_SRC = ROOT / "kernel/con_sink.c"
# K6C-2: 描画抑止は kernel/console.c 側なので、そちらもカーネルと同じ
# フラグでクロスコンパイルする (ホスト側は con_sink_host.c が取り込む)。
CONSOLE_SRC = ROOT / "kernel/console.c"

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-con-sink-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "con-sink"
        subprocess.run(["gcc", *FLAGS, "-O0", "-DCON_SINK_NO_IRQ_LOCK",
                        "-D__KERNEL_BUILD__", *INCLUDES,
                        "-nostdlib", "-static", "-no-pie",
                        str(HOST_SRC), "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST ILP32 GNU11 COMPILE PASS", flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=30).returncode
        # 本番の割込み禁止区間 (include/io.h) を含む形でクロスコンパイル
        subprocess.run(["i386-elf-gcc", *FLAGS, "-D__KERNEL_BUILD__",
                        *INCLUDES, "-O2", "-c", str(KERNEL_SRC),
                        "-o", str(tmp / "con_sink.o")], cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)
        # console.c は既存の警告 (未使用変数・符号比較・TVRAM_BPR 再定義) を
        # 抱えているので -Werror は外す。見たいのは K6C-2 の分岐が
        # カーネルのフラグで通ること。
        subprocess.run(["i386-elf-gcc",
                        *[f for f in FLAGS if f != "-Werror"],
                        "-D__KERNEL_BUILD__", *INCLUDES, "-O2", "-c",
                        str(CONSOLE_SRC), "-o", str(tmp / "console.o")],
                       cwd=ROOT, check=True,
                       stderr=subprocess.DEVNULL)
        print("TARGET i386-elf console.c COMPILE PASS", flush=True)
        print("EXIT con_sink_host=%d" % rc, flush=True)
        sys.exit(rc)
