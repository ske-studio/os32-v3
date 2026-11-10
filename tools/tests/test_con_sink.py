"""K6C: console シンクのリングを実物の kernel/console/con_sink.c で確かめる。

契約: docs/KAPI_SPEC.md (console シンク)
記録: tools/tests/con_sink_tdd.md

test_multiapp_model.py と同じ様式 — ホスト ILP32 GNU11 で走らせたあと、
同じソースがカーネルと同じフラグの i386-elf-gcc -Werror でも通ることを
別に見る ([C1] GNU11)。Make・エミュレータ・libc は使わない。

ホスト側だけ -DCON_SINK_NO_IRQ_LOCK を付ける: CPL=3 では cli/popfl を
実行できないため。クロス側は付けないので、include/io.h を使う本番の経路も
同じ試験の中でコンパイルされる。
"""
from kernel_includes import include_flags
import argparse
import host32
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror"]
# arch/x86 + platform/pc98: include/io.h は契約だけで、実装は固定名
# arch_io.h / platform_io.h を引く (順序 3)。build/config.mk の INC_COMMON と
# 同じものをここでも渡す。
INCLUDES = include_flags(ROOT)
HOST_SRC = ROOT / "tools/tests/con_sink_host.c"
KERNEL_SRC = ROOT / "kernel/console/con_sink.c"
# K6C-2: 描画抑止は kernel/console/console.c 側なので、そちらもカーネルと同じ
# フラグでクロスコンパイルする (ホスト側は con_sink_host.c が取り込む)。
CONSOLE_SRC = ROOT / "kernel/console/console.c"


def run(tmp, source, runner, console_source=None, boot_source=None):
    (tmp / "con_sink.c").write_text(source)
    (tmp / "console.c").write_text(console_source or CONSOLE_SRC.read_text())
    (tmp / "boot_serial.inc").write_text(boot_source or (ROOT / "init/boot_serial.inc").read_text())
    fixture = HOST_SRC.read_text().replace('"../../kernel/console/console.c"', '"console.c"').replace('"../../init/boot_serial.inc"', '"boot_serial.inc"').replace('"../../platform/pc98/console.c"', '"' + str(ROOT / "platform/pc98/console.c") + '"')
    (tmp / "fixture.c").write_text(fixture)
    exe = tmp / "con-sink"
    subprocess.run(["gcc", *FLAGS, "-O0", "-DCON_SINK_NO_IRQ_LOCK",
                    "-D__KERNEL_BUILD__", "-I" + str(tmp), "-I" + str(ROOT / "tools/tests"), *INCLUDES,
                    "-nostdlib", "-static", "-no-pie",
                    str(tmp / "fixture.c"), "-o", str(exe)], cwd=ROOT, check=True)
    return host32.run([str(exe)], cwd=ROOT, runner=runner, timeout=30,
                      capture_output=True, text=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--runner', choices=('native', 'qemu'))
    args = parser.parse_args()
    source = KERNEL_SRC.read_text()
    with tempfile.TemporaryDirectory(prefix="os32-con-sink-") as tmp:
        tmp = pathlib.Path(tmp)
        result = run(tmp, source, args.runner)
        print(result.stdout, end='', flush=True)
        assert result.returncode == 0, result.stderr
        print("HOST ILP32 GNU11 COMPILE PASS", flush=True)
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
        if args.mutate:
            for old, new, expected in [
                ('ring_put(color);', 'ring_put(color ^ 1);', 'FAIL 2e'),
                ('ring_put((u8)CON_SINK_REC_CLEAR);',
                 'ring_put(0x7f);', 'FAIL 2m'),
                ('if (written + size > cap) break;',
                 'if (written + size > cap) size = cap - written;\n'
                 '        if (size == 0) break;', 'FAIL 3j'),
                ('        ring_drop_oldest();',
                 '        ring_drop_oldest();\n'
                 '        if (g_count) ring_drop_oldest();', 'FAIL 5e'),
                ('ring_reset();\n    g_enabled = 0;',
                 'g_enabled = 0;', 'FAIL 6c'),
                ('if (g_reader == id) g_reader = CON_SINK_NO_READER;',
                 '(void)id;', 'FAIL 7f'),
                ('ring_put((u8)id);', 'ring_put((u8)id ^ 1);', 'FAIL 10c'),
            ]:
                assert source.count(old) == 1, old
                result = run(tmp, source.replace(old, new), args.runner)
                assert result.returncode == 1 and expected in result.stdout, result.stdout
                print('RED runtime:', expected, flush=True)
            console = CONSOLE_SRC.read_text()
            boot = (ROOT / "init/boot_serial.inc").read_text()
            for label, old, new in [
                ("print", "if (rshell_active || boot_serial_mirror) con_output_putchar(*str);", "if (rshell_active) con_output_putchar(*str);"),
                ("utf8", "if (rshell_active || boot_serial_mirror) {\n        const char *s", "if (rshell_active) {\n        const char *s"),
                ("write", "if (rshell_active || boot_serial_mirror) {\n        u32 i;", "if (rshell_active) {\n        u32 i;"),
            ]:
                assert console.count(old) == 1
                result = run(tmp, source, args.runner, console_source=console.replace(old, new))
                assert result.returncode == 1 and "boot serial backlog then live" in result.stdout, result.stdout
                print("RED runtime: " + label, flush=True)
            for label, old, new in [("default", '!= 1) return;', '== 1) return;'),
                                    ("enable", 'boot_serial_mirror = 1;', 'boot_serial_mirror = 0;')]:
                assert boot.count(old) == 1
                result = run(tmp, source, args.runner, boot_source=boot.replace(old, new))
                assert result.returncode == 1 and "boot serial" in result.stdout, result.stdout
                print("RED runtime: " + label, flush=True)
        print("EXIT con_sink_host=0", flush=True)
