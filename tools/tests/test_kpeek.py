"""kdebug の peek 本体 (kdebug/kpeek.c・kpeek_allow.c) のホスト試験。

票: docs/tasks/v3/TASK_KPEEK.md (詳細設計 §2-4・§2-5・§4)

tools/tests/kpeek_host.c が実物の kpeek.c と kpeek_allow.c を 1 行も写さずに
#include し、import (paging_* / pci_* / irq_save / irq_restore) と読みだけを
贋物にして回す。幅・長さ・整列の拒否、NOMAP / PCD・PWT / [A0000h, E8000h) の
DENY (読まない・fail_addr はページ先頭)、許可リストの行と幅、BAR の 3 条件、
4MB 境界またぎの ARG、窓の順序 (irq_save → 退避 → 張る → 読む → 戻す →
irq_restore、失敗でも戻す)、窓の flags に RW が無いことを見る。
LAN の出口 (T5、§2-8・§4): 偽 e82557_send でフレームを捕り、Ethernet・SFS の
封筒 (T_DUMP、tag、通し番号 0 から)・addr・data・CRC32 (種別〜末尾)・60 B の
詰め物、AGAIN だけ 2 tick 再試行で LAN (7)、他の負は即 LAN、probe は RUNNING
かつ宛先 (明示かペア) のときだけ OK。
同じ 2 ファイルを i386-elf-gcc -Wall -Wextra -Werror でも通す (実物の io.h・P2V_IO)。

  python3 -B tools/tests/test_kpeek.py            # ホスト + i386-elf
  python3 -B tools/tests/test_kpeek.py --mutate   # 否定側 (変異が RED になるか)
"""
CHECK = dict(name='kpeek-host', tier=1,
    covers=['kdebug/kpeek.c', 'kdebug/kpeek.h', 'kdebug/kpeek_allow.c', 'kdebug/kpeek_allow.h', 'mm/paging.h', 'drivers/pci/pci.h', 'drivers/pci/pci_decode.h', 'include/memmap.h', 'include/pc98.h', 'include/io.h', 'sdk/include/os32/os32_kapi_shared.h', 'lib/crc32_core.inc', 'tools/tests/kpeek_host.c', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'kdebug/kdebug_window.c', 'kdebug/kdebug_window.h', 'kdebug/recover.h', 'include/pci_dev_contract.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=1.591064)

from kernel_includes import include_flags
import host32
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar                                                   # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "tools/tests/kpeek_host.c"
KPEEK = "kdebug/kpeek.c"
ALLOW = "kdebug/kpeek_allow.c"
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-attributes"]
# build/config.mk の INC_KERNEL と同じ探索先 (+ kdebug)。
INCLUDES = (['-I' + str(ROOT / p) for p in ('kdebug',)] + include_flags(ROOT))


def cross_gcc():
    cross = os.environ.get("CROSS_DIR")
    if cross and (pathlib.Path(cross) / "bin/i386-elf-gcc").exists():
        return str(pathlib.Path(cross) / "bin/i386-elf-gcc")
    return shutil.which("i386-elf-gcc")


def host_cmd(exe, probe=False):
    # ILP32 (u32 = unsigned long が 32 bit)、libc なし (cirrus_win と同じ)。
    return ["gcc", *FLAGS, *(["-DGF_VPLL_PROBE=1"] if probe else []), "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
            "-fno-stack-protector", "-O0", "-nostdlib", "-static", "-no-pie",
            *INCLUDES, str(SRC), "-o", str(exe)]


MUTATIONS = [
    ("vpll_allow", ALLOW, "    NV_ROW(KPEEK_NV_VPLL_CONTROL_OFF, KPEEK_NV_VPLL_CONTROL_LEN, KPEEK_W4),", ""),
    # (名前, 対象, 元, 変異後)
    ("chunk_ge", KPEEK,
     "req->len > KDEBUG_PEEK_CHUNK", "req->len >= KDEBUG_PEEK_CHUNK"),
    ("no_restore", "kdebug/kdebug_window.c",
     "w->ops->unmap(w->opaque, w->va + i * MEM_PAGE_SIZE, w->raw[i])", "0"),
    ("width_mask_flip", ALLOW,
     "if (!(r->width_mask & width)) continue;",
     "if (r->width_mask & width) continue;"),
    ("present_first_only", KPEEK,
     "if (!paging_is_present(page))",
     "if (page == (req->addr & ~KPEEK_PAGE_MASK) && !paging_is_present(page))"),
    ("no_pcd_check", KPEEK,
     "        else if (paging_pte_flags(page) & (PTE_PCD | PTE_PWT))\n"
     "            st = KDEBUG_PEEK_DENY;\n", ""),
    ("no_mem_enable", ALLOW,
     "    if (!(d.command & PCI_CMD_MEM_ENABLE)) return 0;\n", ""),
    ("pde_ge", KPEEK,
     "+ req->len > MEM_DEVICE_APERTURE_PDE_SIZE", "+ req->len >= MEM_DEVICE_APERTURE_PDE_SIZE"),
    ("no_legacy_deny", KPEEK,
     "        else if (page >= KPEEK_RAM_DENY_LO && page < KPEEK_RAM_DENY_HI)\n"
     "            st = KDEBUG_PEEK_DENY;\n", ""),
    ("win_rw", "kdebug/kdebug_window.c",
     "(write ? PTE_RW : 0)", "(write ? PTE_RW : PTE_RW)"),
    ("no_io_bar", ALLOW,
     "    if (bar & KPEEK_BAR_IO_BIT) return 0;\n", ""),
    # T5 (LAN の出口)
    ("lan_crc_shift", KPEEK,
     "crc32_core_update(CRC32_INIT, sfs + 3,", "crc32_core_update(CRC32_INIT, sfs + 4,"),
    ("lan_seq_init", KPEEK,
     "        kpeek_lan_seq = 0;\n", "        kpeek_lan_seq = 1;\n"),
    ("lan_retry_ticks", KPEEK,
     "#define KPEEK_LAN_RETRY_TICKS 2U", "#define KPEEK_LAN_RETRY_TICKS 3U"),
    ("lan_retry_busy", KPEEK,
     "if (rc != OS32_ERR_AGAIN ||", "if ((rc != OS32_ERR_AGAIN && rc != OS32_ERR_BUSY) ||"),
    ("lan_no_tag_reset", KPEEK,
     "if (!kpeek_lan_have || req->tag != kpeek_lan_tag) {", "if (!kpeek_lan_have) {"),
    ("lan_probe_any_state", KPEEK,
     "    if (state != E82557_RUNNING) return KDEBUG_PEEK_LAN;\n", ""),
    ("lan_no_pad", KPEEK,
     "    while (size < SFS_LAN_MIN_FRAME) f[size++] = 0;\n", ""),
]


def one_mutation(item):
    name, target, old, new = item
    original = (ROOT / target).read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-20s SKIP (目印が見つからない)" % name, 1
    with tempfile.TemporaryDirectory(prefix="os32-kpeek-mut-") as td:
        exe = pathlib.Path(td) / ("mut-" + name)
        try:
            tree = mutpar.build_in_tree(
                ROOT, td, {target: original.replace(old, new, 1)},
                [host_cmd(exe, name == "vpll_allow")], capture_output=True)
        except subprocess.CalledProcessError:
            return "MUTATE %-20s RED (コンパイルが通らない)" % name, 0
        out = host32.run([str(exe)], cwd=str(tree), timeout=60,
                         capture_output=True)
    if out.returncode == 0:
        return ("MUTATE %-20s **GREEN のまま = 試験が規則を見ていない**"
                % name, 1)
    last = out.stdout.decode("utf-8", "replace").strip().splitlines()[-1:]
    return "MUTATE %-20s RED (期待どおり落ちた: %s)" % (
        name, last[0] if last else "rc=%d" % out.returncode), 0


def main():
    failed = 0
    with tempfile.TemporaryDirectory(prefix="os32-kpeek-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "kpeek-host"
        subprocess.run(host_cmd(exe), cwd=ROOT, check=True)
        print("HOST ILP32 GNU11 COMPILE PASS (real kdebug/kpeek.c + kpeek_allow.c)",
              flush=True)
        rc = host32.run([str(exe)], cwd=ROOT, timeout=60).returncode
        if rc == 0:
            subprocess.run(host_cmd(exe, True), cwd=ROOT, check=True)
            rc = host32.run([str(exe)], cwd=ROOT, timeout=60).returncode
        print("EXIT kpeek_host=%d" % rc, flush=True)
        failed += rc != 0
        gcc = cross_gcc()
        if not gcc:
            print("TARGET i386-elf-gcc が無い = FAIL (CROSS_DIR を渡す)")
            failed += 1
        else:
            for src in (KPEEK, ALLOW, "kdebug/kdebug_window.c"):
                subprocess.run([gcc, *FLAGS, "-m32", "-march=i386",
                                "-ffreestanding", "-fno-pie",
                                "-fno-stack-protector", "-O2",
                                "-D__KERNEL_BUILD__", *INCLUDES, "-c",
                                str(ROOT / src), "-o",
                                str(tmp / (pathlib.Path(src).stem + ".o"))],
                               cwd=ROOT, check=True)
            print("TARGET i386-elf GNU11 -Wextra -Werror COMPILE PASS "
                  "(kpeek.c + kpeek_allow.c)", flush=True)
        if "--mutate" in sys.argv:
            failed += mutpar.run_with_control(one_mutation, MUTATIONS,
                                              ("control", KPEEK, "", ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kpeek.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
