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
                      "kernel", "exec", "lib", "sdk/include/os32")]
HOST_SRC = ROOT / "tools/tests/kbd_inject_host.c"
KERNEL_SRC = ROOT / "kernel/kbd_inject.c"

def run(tmp, source):
    (tmp / "kbd_inject.c").write_text(source)
    exe = tmp / "kbd-inject"
    subprocess.run(["gcc", *FLAGS, "-O0", "-DCON_SINK_NO_IRQ_LOCK",
                    "-DKBD_INJECT_NO_IRQ_LOCK", "-D__KERNEL_BUILD__",
                    "-I"+str(tmp), *INCLUDES, "-nostdlib", "-static", "-no-pie",
                    str(HOST_SRC), "-o", str(exe)], cwd=ROOT, check=True)
    return host32.run([str(exe)], cwd=ROOT, timeout=30, capture_output=True, text=True)

if __name__ == "__main__":
    import argparse
    p = argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true')
    args = p.parse_args()
    source = KERNEL_SRC.read_text()
    with tempfile.TemporaryDirectory(prefix="os32-kbd-inject-") as directory:
        tmp = pathlib.Path(directory)
        result = run(tmp, source)
        print(result.stdout, end=''); assert result.returncode == 0
        subprocess.run(["i386-elf-gcc", *FLAGS, "-D__KERNEL_BUILD__", *INCLUDES,
                        "-O2", "-c", str(KERNEL_SRC), "-o", str(tmp / "kbd_inject.o")],
                       cwd=ROOT, check=True)
        if args.mutate:
            for old, new, expected in [
                ('return ring3_wm_depth > 0 ? APP_ID_SHELL : appslot_cur();', 'return appslot_cur();',
                 'FAIL WM pump cannot borrow fullscreen reader identity'),
                ('if (appslot_gfx_owner() >= APP_ID_MIN) bad |= 1u << 0;', '',
                 'FAIL boot rejects fullscreen precondition'),
                ('if (allowed[g_inj_dest[pos]]) {', 'if (1) {',
                 'FAIL hidden sh cannot take'),
                ('if (allowed[g_inj_dest[pos]]) n++;', 'n++;',
                 'FAIL hidden sh pending is empty'),
                ('dest != id && !(dest == 0 && reader_exit)',
                 'dest != id && !(dest == 0 && (reader_exit || appslot_gfx_owner() == id))',
                 'FAIL fullscreen descendant exit preserves terminal typeahead'),
                ('if (ring3_wm_depth > 0) return (i32)OS32_ERR_EXIST;', '',
                 'FAIL WM pump cannot borrow terminal authority'),
                ('dest != id && !(dest == 0 && reader_exit)', '!(dest == 0 && reader_exit)',
                 'FAIL fullscreen exit discards tail'),
                ('if (child == id) return 0;', '',
                 'FAIL async terminal descendant no duplicate'),
                ('if (ring3_wm_depth <= 0 && appslot_cur() != APP_ID_SHELL) return 0;', '',
                 'FAIL fullscreen rejects non WM'),
                ('if (id == reader) return 0;', '(void)reader;',
                 'FAIL terminal descendant no duplicate'),
                ('if (id == APP_ID_SHELL) return 1;', 'if (id == APP_ID_SHELL) return 0;',
                 'FAIL fullscreen WM without reader'),
            ]:
                assert source.count(old) == 1
                result = run(tmp, source.replace(old, new))
                assert result.returncode != 0 and expected in result.stdout, result.stdout
                print('RED runtime:', expected)
        print('PASS kbd inject and target compile')
