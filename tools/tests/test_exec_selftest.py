#!/usr/bin/env python3
"""Run moved exec tests through real generated import slots/stubs on ILP32.

Compile the real exec TU with section GC: retain snapshot, trampoline and
string/WM paths, replacing only their environment. No emulator or mutants.
"""
from kernel_includes import include_flags
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import host32

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_kdebug_imports as gen

IMPORTS = (
    'kdi_selftest_exec_view', 'paging_pte_flags', 'vfs_cwd_user', 'vfs_cwd',
    'vfs_devname_user', 'vfs_devname', 'kstrcmp', 'path_get_drive_user',
    'path_get_drive', 'path_get_cwd_user', 'path_get_cwd', 'ring3_wm_enter',
    'ring3_wm_leave', 'kdi_get_ring3_in_syscall', 'kdi_set_ring3_in_syscall',
    'kapi_reserved_nosys', 'kmemset', 'os32x_layout_check',
    'kdi_ptr_ring3_ls_start', 'kdi_ptr_ring3_ls_end',
    'kdi_ptr___kernel_resident_end',
)

MAIN = r'''
#include "kdebug/generated/kdebug_import_macros.h"
#include "kdebug/selftest_internal.h"
#include "ring3_str.h"
void host_init(void);
void host_flags_set(u32);
void host_string_set(int);
int host_restored(void);
@DECLS@
static void die(int line) {
    char b[12]; int n = 0;
    do { b[n++] = '0' + line % 10; line /= 10; } while (line);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(b), "d"(n) : "memory");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(1));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) die(__LINE__); } while (0)
static int broken_layout(const OS32Header *h, u32 n, u32 off)
{ (void)h; (void)n; (void)off; return OS32X_LAYOUT_MISMATCH; }
void _start(void) {
    @SLOTS@
    struct kselftest_exec_view before, after;
    kdi_selftest_exec_view(&before);
    CHECK(before.tramp_page == 0 && before.api == 0);
    CHECK(kst_exec_tramp_user() == 1 && kst_exec_kapi_layout() == 15);
    host_init();
    kdi_selftest_exec_view(&before);
    CHECK(before.tramp_page && before.api == (const KernelAPI *)KAPI_ADDR);
    CHECK(KDI_ADDR_kapi_reserved_nosys == host_kapi_reserved_nosys);
    CHECK(KDI_ADDR_kapi_reserved_nosys != kapi_reserved_nosys);
    CHECK(kapi_reserved_nosys() == OS32_ERR_NOSYS);
    CHECK(ring3_ls_start == host_kdi_ptr_ring3_ls_start());
    CHECK(ring3_ls_end == host_kdi_ptr_ring3_ls_end());
    CHECK(ring3_ls_end - ring3_ls_start > 1);
    CHECK(&__kernel_resident_end == host_kdi_ptr___kernel_resident_end());
    for (int saved = 0; saved < 3; saved++) {
        kdi_set_ring3_in_syscall(saved);
        CHECK(kst_exec_tramp_user() == 0);
        CHECK(ring3_in_syscall == saved && host_restored());
        CHECK(kst_exec_kapi_layout() == 0);
        kdi_selftest_exec_view(&after);
        CHECK(before.api == after.api && before.tramp_page == after.tramp_page);
    }
    host_flags_set(PTE_PRESENT);
    CHECK(kst_exec_tramp_user() == (1u << 1));
    host_flags_set(PTE_USER);
    CHECK(kst_exec_tramp_user() == (1u << 1));
    host_flags_set(PTE_PRESENT | PTE_USER);
    host_string_set(1);
    CHECK(kst_exec_tramp_user() == ((1u << 2) | (1u << 5)));
    host_string_set(0);
    CHECK(ring3_in_syscall == 2 && host_restored());
    KernelAPI *api = (KernelAPI *)before.api;
    u32 *tbl = (u32 *)before.tramp_page;
    tbl[KAPI_DATA_IDX_SHM_BASE] ^= 1;
    CHECK(kst_exec_kapi_layout() == 1);
    tbl[KAPI_DATA_IDX_SHM_BASE] ^= 1;
#if KAPI_FUNC_RESERVED > 0
    api->kapi_reserved[0] = kapi_reserved_nosys; /* stub address is not the kernel address */
    CHECK(kst_exec_kapi_layout() == (1u << 1));
    api->kapi_reserved[0] = KDI_ADDR_kapi_reserved_nosys;
    u8 *st = (u8 *)(before.tramp_page + RING3_USTR_STUB_OFF +
                    KAPI_FUNC_COUNT * KAPI_STUB_SIZE);
    st[5] ^= 1;
    CHECK(kst_exec_kapi_layout() == (1u << 2));
    st[5] ^= 1;
#endif
    u8 *shim = (u8 *)(before.tramp_page + RING3_LS_SHIM_OFF);
    shim[ring3_ls_end - ring3_ls_start - 1] ^= 1;
    CHECK(kst_exec_kapi_layout() == (1u << 2));
    shim[ring3_ls_end - ring3_ls_start - 1] ^= 1;
    kdebug_import_slots[@LAYOUT_INDEX@] = (uptr)broken_layout;
    CHECK(kst_exec_kapi_layout() == (1u << 3));
    kdebug_import_slots[@LAYOUT_INDEX@] = (uptr)host_os32x_layout_check;
    CHECK(kst_exec_kapi_layout() == 0 && kst_exec_tramp_user() == 0);
    CHECK(ring3_in_syscall == 2 && host_restored());
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
'''


class ExecSelftest(unittest.TestCase):
    def test_generated_import_execution(self):
        entries = gen.load_def()
        by_name = {e['name']: e for e in entries}
        cross = Path(os.environ['CROSS_DIR']) / 'bin'
        flags = ['-m32', '-march=i386', '-std=gnu11', '-ffreestanding',
                 '-fno-pie', '-fno-stack-protector', '-fcommon', '-O2',
                 '-ffunction-sections', '-fdata-sections', '-D__KERNEL_BUILD__']
        flags += include_flags(ROOT)
        with tempfile.TemporaryDirectory(prefix='exec-selftest-') as folder:
            out = Path(folder)
            main = MAIN.replace('@DECLS@', '\n'.join(
                by_name[n]['signature'].replace(n + '(', 'host_' + n + '(')
                for n in IMPORTS))
            main = main.replace('@SLOTS@', '\n'.join(
                f"kdebug_import_slots[{by_name[n]['index']}] = (uptr)host_{n};"
                for n in IMPORTS))
            main = main.replace('@LAYOUT_INDEX@', str(by_name['os32x_layout_check']['index']))
            (out / 'main.c').write_text(main)
            # Keep real kernel implementations distinct from the module's stubs.
            kernel_sources = '\n'.join((ROOT / p).read_text() for p in (
                'exec/exec.c', 'exec/ring3_str.c', 'exec/os32x_hdr.c',
                'tools/tests/exec_selftest_host.c'))
            definitions = {e['name'] for e in entries if re.search(
                r'\b' + re.escape(e['name']) + r'\s*\([^;{}]*\)\s*\{', kernel_sources)}
            renames = ['-D' + n + '=host_' + n for n in sorted(set(IMPORTS) | definitions)]
            objects = []
            sources = [(ROOT / 'tools/tests/exec_selftest_host.c', renames),
                       (ROOT / 'exec/ring3_ls.S', []),
                       (ROOT / 'kdebug/selftest_exec.c', []),
                       (ROOT / 'kdebug/generated/kdebug_import_slots.c', []),
                       (ROOT / 'kdebug/generated/kdebug_import_stubs.S', []),
                       (out / 'main.c', [])]
            for i, (source, extra) in enumerate(sources):
                obj = out / f'{i}.o'
                result = subprocess.run([str(cross / 'i386-elf-gcc'), *flags,
                    *extra, '-c', str(source), '-o', str(obj)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                objects.append(str(obj))
            exe = out / 'exec-selftest'
            result = subprocess.run([str(cross / 'i386-elf-ld'), '-m', 'elf_i386',
                '--gc-sections', '--defsym=' + gen.ABI_SYMBOL + '=0',
                '-o', str(exe), *objects], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = host32.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
