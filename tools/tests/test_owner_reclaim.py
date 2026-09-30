"""K5b-K: owner-ID reclamation closes on one app, using the real sources.

Ticket: docs/archive/gui_v13/TASK_K5B_kernel.md (host tests, 2nd item)
Design: TASK_K5_multiapp.md D3 (the `*_owned(id)` inventory, P3/P5)
Log:    tools/tests/k5b_kernel_tdd.md

Compiles the shipped fs/fd_redirect.c, fs/pipe_buffer.c and kernel/shm.c
(included verbatim by owner_reclaim_host.c) with a minimal kernel environment,
then checks that folding ID 2 frees only ID 2's redirect / pipe / SHM.

Same harness shape as test_kapi_db_owned.py: tiny shim headers in a temp dir,
one host binary, no Make / emulator / VFS.
"""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class OwnerReclaimTests(unittest.TestCase):
    def test_host_cases(self):
        with tempfile.TemporaryDirectory(prefix="os32-owner-reclaim-") as tmp:
            tmp = pathlib.Path(tmp)
            # kernel/shm.c derives every address from MEM_SHM_BASE and the
            # STATIC_ASSERTs need a constant expression, so keep the real
            # numeric shape. Nothing is dereferenced: kmemset is a recording
            # no-op and paging_map_range is stubbed in the host file.
            (tmp / "memmap.h").write_text(
                "#ifndef OS32_TEST_MEMMAP_H\n"
                "#define OS32_TEST_MEMMAP_H\n"
                "#define MEM_SHM_BASE       0x00180000U\n"
                # 2026-09-17 決裁 D1 で 16 → 14 ブロック、GUI 予約は末尾 4 個。
                # 値の写しなので tools/gen_memmap.py --check が実物と照合する。
                "#define MEM_SHM_SIZE       0x038000U\n"   # 224KB = 16KB x 14
                "#define MEM_SHM_GUI_SIZE   0x10000U\n"
                "#define MEM_SHM_GUI_OFFSET (MEM_SHM_SIZE - MEM_SHM_GUI_SIZE)\n"
                "#define MEM_SHM_GUI_BASE   (MEM_SHM_BASE + MEM_SHM_GUI_OFFSET)\n"
                "#define MEM_SHM_GUARD_LO   (MEM_SHM_BASE - 0x1000U)\n"
                "#define MEM_SHM_GUARD_HI   (MEM_SHM_BASE + MEM_SHM_SIZE)\n"
                "#define GUI_SLOT_SIZE      0x4000U\n"
                "#define GUI_SLOT_MAX       4\n"
                # kernel/paging.h (included by shm.c from its own directory,
                # so the real header always wins) needs this one constant.
                "#define MEM_APP_BAND_MAX_PDES 2UL\n"
                "#endif\n")
            (tmp / "kstring.h").write_text(
                "#include <string.h>\n"
                "void test_shm_memset(void *d, int c, unsigned long n);\n"
                "#define kmemset(d,c,n) test_shm_memset((d),(c),(n))\n"
                "#define kmemcpy(d,s,n) memcpy((d),(s),(n))\n")
            (tmp / "kmalloc.h").write_text(
                "#include \"types.h\"\nvoid *kmalloc(u32 size);\nvoid kfree(void *p);\n")
            exe = tmp / "owner-reclaim"
            subprocess.run([
                "cc", "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", "-Wdeclaration-after-statement",
                "-D__cdecl=",
                "-I" + str(tmp),
                "-I" + str(ROOT / "sdk/include/os32"),
                "-I" + str(ROOT / "include"),
                "-I" + str(ROOT / "fs"),
                "-I" + str(ROOT / "kernel"),
                str(ROOT / "tools/tests/owner_reclaim_host.c"), "-o", str(exe),
            ], check=True, cwd=ROOT)
            subprocess.run([str(exe)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
