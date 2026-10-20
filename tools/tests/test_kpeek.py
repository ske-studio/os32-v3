"""kdebug の peek 本体 (kdebug/kpeek.c・kpeek_allow.c) のホスト試験。

票: docs/tasks/v3/TASK_KPEEK.md (詳細設計 §2-4・§2-5・§4)

tools/tests/kpeek_host.c が実物の kpeek.c と kpeek_allow.c を 1 行も写さずに
#include し、import (paging_* / pci_* / irq_save / irq_restore) と読みだけを
贋物にして回す。幅・長さ・整列の拒否、NOMAP / PCD・PWT / [A0000h, E8000h) の
DENY (読まない・fail_addr はページ先頭)、許可リストの行と幅、BAR の 3 条件、
4MB 境界またぎの ARG、窓の順序 (irq_save → 退避 → 張る → 読む → 戻す →
irq_restore、失敗でも戻す)、窓の flags に RW が無いことを見る。
同じ 2 ファイルを i386-elf-gcc -Wall -Wextra -Werror でも通す (実物の io.h・P2V_IO)。

  python3 -B tools/tests/test_kpeek.py            # ホスト + i386-elf
  python3 -B tools/tests/test_kpeek.py --mutate   # 否定側 (変異が RED になるか)
"""
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
INCLUDES = ["-I" + str(ROOT / p) for p in
            (".", "include", "arch/x86", "platform/pc98", "sdk/include",
             "sdk/include/os32", "kernel", "drivers", "net", "fs", "exec",
             "gfx", "lib", "kapi", "kdebug")]


def cross_gcc():
    cross = os.environ.get("CROSS_DIR")
    if cross and (pathlib.Path(cross) / "bin/i386-elf-gcc").exists():
        return str(pathlib.Path(cross) / "bin/i386-elf-gcc")
    return shutil.which("i386-elf-gcc")


def host_cmd(exe):
    # ILP32 (u32 = unsigned long が 32 bit)、libc なし (cirrus_win と同じ)。
    return ["gcc", *FLAGS, "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
            "-fno-stack-protector", "-O0", "-nostdlib", "-static", "-no-pie",
            *INCLUDES, str(SRC), "-o", str(exe)]


MUTATIONS = [
    # (名前, 対象, 元, 変異後)
    ("chunk_ge", KPEEK,
     "req->len > KDEBUG_PEEK_CHUNK", "req->len >= KDEBUG_PEEK_CHUNK"),
    ("no_restore", KPEEK,
     "        if (paging_pte_restore(va0 + i * PAGE_SIZE, raw[i]) != 0)\n"
     "            st = KDEBUG_PEEK_WINDOW;\n",
     "        (void)raw[i];\n"),
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
    ("win_rw", KPEEK,
     "#define KPEEK_WIN_FLAGS   (PTE_PRESENT | PTE_PCD)",
     "#define KPEEK_WIN_FLAGS   (PTE_PRESENT | PTE_RW | PTE_PCD)"),
    ("no_io_bar", ALLOW,
     "    if (bar & KPEEK_BAR_IO_BIT) return 0;\n", ""),
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
                [host_cmd(exe)], capture_output=True)
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
        print("EXIT kpeek_host=%d" % rc, flush=True)
        failed += rc != 0
        gcc = cross_gcc()
        if not gcc:
            print("TARGET i386-elf-gcc が無い = FAIL (CROSS_DIR を渡す)")
            failed += 1
        else:
            for src in (KPEEK, ALLOW):
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
