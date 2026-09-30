"""Cirrus の窓の可否は RAM の上端ではなく物理地図で決める (gfx/backend_cirrus.c)。

記録: tools/tests/cirrus_win_tdd.md / 教訓 docs/POLICY_DEBUG.md §4-34

tools/tests/cirrus_win_host.c が実物の gfx/backend_cirrus.c を 1 行も写さずに
#include し、pgalloc_range_has_ram (贋の物理地図) と sys_get_mem_kb (上端) と
ボードグルーだけを贋物にして ILP32 で回す (test_pgalloc_range.py と同じ形)。

  python3 -B tools/tests/test_cirrus_win.py [--mutate]

--mutate は否定側。判定を壊した版 (上端で見る旧判定に戻す / 物理地図を
見ない / 窓の末尾ページを落とす / 窓の前のページまで広げる / 32bit の末尾越えを
見ない / auto で NP21/W 判定を飛ばす / GFX=cirrus を無視する / 窓より先に判定を
読む / リニア窓を旧番地へ戻す / probe が ⑥ の SURFACE を確かめない) を写しの木で組み、この試験が RED になることを見る。
make・エミュレータ・配備には触れない。
"""
import os
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar                                                   # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror"]
# build/config.mk の INC_GFX と同じ探索先 (INC_COMMON + gfx drivers fs lib kernel)。
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "arch/x86", "platform/pc98",
                      "sdk/include/os32",
                      "gfx", "drivers", "fs", "lib", "kernel")]
SRC = ROOT / "tools/tests/cirrus_win_host.c"
MUT_TARGET = "gfx/backend_cirrus.c"


def host_cmd(exe):
    return ["gcc", *FLAGS, "-Wno-unused-function", "-O0", "-nostdlib",
            "-static", "-no-pie", *INCLUDES, str(SRC), "-o", str(exe)]


def target_cmd(obj):
    return ["i386-elf-gcc", *FLAGS, "-O2", *INCLUDES,
            "-c", str(ROOT / MUT_TARGET), "-o", str(obj)]


MUTATIONS = [
    # (名前, 対象ファイル, 元, 変異後)
    # 1 = 本件の欠陥そのもの: RAM の上端 (sys_get_mem_kb) で窓を決める旧判定。
    ("top_of_ram", MUT_TARGET,
     "    return !pgalloc_range_has_ram(base / PAGE_SIZE, last / PAGE_SIZE + 1);",
     "    { extern u32 sys_get_mem_kb(void); (void)last;\n"
     "      return !(sys_get_mem_kb() > base / 1024UL); }"),
    # 2 = 物理地図を見ない (窓は常に空いている扱い)。
    ("no_map", MUT_TARGET,
     "    return !pgalloc_range_has_ram(base / PAGE_SIZE, last / PAGE_SIZE + 1);",
     "    (void)last; return 1;"),
    # 3 = 窓の末尾ページを問い合わせから落とす (部分一致の見落とし)。
    ("end_short", MUT_TARGET,
     "last / PAGE_SIZE + 1);", "last / PAGE_SIZE);"),
    # 4 = 窓の前のページまで問い合わせる (隣の RAM で窓を塞ぐ)。
    ("first_early", MUT_TARGET,
     "pgalloc_range_has_ram(base / PAGE_SIZE, last",
     "pgalloc_range_has_ram(base / PAGE_SIZE - 1, last"),
    # 5 = 32bit 空間の末尾越えを見ない (末尾番地が桁あふれして小さく見える)。
    ("no_wrap_check", MUT_TARGET,
     "    if (size - 1 > 0xFFFFFFFFUL - base) return 0;\n", ""),
    # 6 = auto でも NP21/W 判定をせずに ID を読む (実機でポートを叩く)。
    ("no_np2_gate", MUT_TARGET,
     "    if (gfx_get_backend_pref() != GFX_PREF_CIRRUS && !np2_detect()) return 0;\n",
     ""),
    # 7 = GFX=cirrus の明示でも NP21/W でなければ試さない (利用者の指定を無視)。
    ("gate_ignores_forced", MUT_TARGET,
     "gfx_get_backend_pref() != GFX_PREF_CIRRUS && !np2_detect()",
     "!np2_detect()"),
    # 8 = 窓の判定より前に NP21/W 判定を読む (RAM に当たる構成でも I/O を出す)。
    ("np2_before_window", MUT_TARGET,
     "    if (!cirrus_win_usable(s_glue->win_base, s_glue->win_size)) return 0;\n",
     "    if (gfx_get_backend_pref() != GFX_PREF_CIRRUS && !np2_detect()) return 0;\n"
     "    if (!cirrus_win_usable(s_glue->win_base, s_glue->win_size)) return 0;\n"),
    # 10 = probe が ⑥ の SURFACE (予約・写像済み) を確かめずに I/O へ進む
    #      (T1e、TASK_T1_LEDGER §3-8: probe は自分で予約も写像もしない)。
    ("no_surface_gate", MUT_TARGET,
     "    if (!cirrus_identify() ||\n"
     "        !ledger_surface_find(LEDGER_SF_CIRRUS, LEDGER_ROLE_CLIENT)) return 0;\n",
     "    if (!cirrus_identify()) return 0;\n"),
    # 9 = リニア窓を 16MB 直上 (旧番地) へ戻す。高位 RAM の構成で probe が落ちる
    #     (帯の外なので STATIC_ASSERT でも止まる)。
    ("old_linear_sel", "include/wab_xe10.h",
     "#define WAB_XE10_LINEARWIN_SEL \\\n"
     "    ((u8)(MEM_DEVICE_APERTURE_BASE >> WAB_XE10_LINEARWIN_SHIFT))",
     "#define WAB_XE10_LINEARWIN_SEL 0x01"),
]


def one_mutation(item):
    name, target, old, new = item
    original = (ROOT / target).read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-20s SKIP (目印が見つからない)" % name, 1
    with tempfile.TemporaryDirectory(prefix="os32-cirrus-win-mut-") as td:
        exe = pathlib.Path(td) / ("mut-" + name)
        try:
            tree = mutpar.build_in_tree(
                ROOT, td, {target: original.replace(old, new, 1)},
                [host_cmd(exe)], capture_output=True)
        except subprocess.CalledProcessError:
            return "MUTATE %-20s RED (コンパイルが通らない)" % name, 0
        out = subprocess.run([str(exe)], cwd=str(tree), timeout=60,
                             capture_output=True)
    if out.returncode == 0:
        return ("MUTATE %-20s **GREEN のまま = 試験が規則を見ていない**"
                % name, 1)
    last = out.stdout.decode("utf-8", "replace").strip().splitlines()[-1:]
    return "MUTATE %-20s RED (期待どおり落ちた: %s)" % (
        name, last[0] if last else "rc=%d" % out.returncode), 0


if __name__ == "__main__":
    failed = 0
    with tempfile.TemporaryDirectory(prefix="os32-cirrus-win-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "cirrus-win"
        subprocess.run(host_cmd(exe), cwd=ROOT, check=True)
        print("HOST ILP32 GNU11 COMPILE PASS (real gfx/backend_cirrus.c)",
              flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=60).returncode
        print("EXIT cirrus_win_host=%d" % rc, flush=True)
        failed += rc != 0
        subprocess.run(target_cmd(tmp / "backend_cirrus.o"), cwd=ROOT,
                       check=True)
        # ボードの層: リニア窓が v3 のデバイス窓の帯と静的 PT に収まることは
        # drivers/wab_glue_xe10.c の STATIC_ASSERT が見る。同じフラグで通す。
        subprocess.run(["i386-elf-gcc", *FLAGS, "-O2", *INCLUDES, "-c",
                        str(ROOT / "drivers/wab_glue_xe10.c"),
                        "-o", str(tmp / "wab_glue_xe10.o")], cwd=ROOT,
                       check=True)
        print("TARGET i386-elf GNU11 -Werror COMPILE PASS "
              "(backend_cirrus.c + wab_glue_xe10.c)", flush=True)
        if "--mutate" in sys.argv:
            failed += mutpar.run_with_control(one_mutation, MUTATIONS,
                                              ("control", MUT_TARGET, "", ""))
    sys.exit(1 if failed else 0)
