"""Freestanding ILP32 real SQLite/VFS/IME fixture construction for N/O2."""
import os
from pathlib import Path
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]


def prepare(work, probe=None, observe=False):
    cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    host32.build([cc, '-m32', '-march=i386', '-std=gnu89', '-Os', '-DNDEBUG',
        '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-w',
        '-fsigned-char', '-fno-short-enums', '-include',
        str(ROOT / 'lib/sqlite3/os32_sqlite_config.h'), '-c',
        str(ROOT / 'lib/sqlite3/sqlite3.c'), '-o', str(work / 'sqlite.o')], check=True)
    shim = work / 'ime_shim'
    shim.mkdir()
    (shim / 'memmap.h').write_text('#include "' + str(ROOT / 'include/memmap.h') + '"\n'
        'extern unsigned char test_shm[];\n#undef MEM_SHM_BASE\n#define MEM_SHM_BASE test_shm\n')
    for header in ('stdio.h', 'stdlib.h', 'string.h'):
        (work / header).write_text('#include "fep_boundary_runtime.h"\n')
    harness = (ROOT / 'tools/tests/ime_dict_host.c').read_text()
    harness = harness.replace('#include "../../lib/', '#include "' + str(ROOT / 'lib') + '/')
    harness = harness.replace('#include "../../kernel/', '#include "' + str(ROOT / 'kernel') + '/')
    start = harness.index('#define __KSTRING_H')
    end = harness.index('#include "' + str(ROOT / 'lib'), start)
    harness = harness[:start] + ('void *kzalloc(u32 n) { return calloc(1, n); }\n'
        'void kfree(void *p) { free(p); }\n') + harness[end:]
    if probe is not None:
        harness = harness.replace('#include "fep_store_host.h"',
            '#include "kernel/ime/ime_conn.c"\n#include "kernel/ime/ime_user_store.c"\n#include "probe.c"')
        (work / 'probe.c').write_text(probe)
    if observe:
        vfs = (ROOT / 'lib/sqlite3/os32_sqlite_vfs.c').read_text()
        anchor = '    budget->live[tag->owner] += tag->charge;'
        assert vfs.count(anchor) == 1
        vfs = vfs.replace(anchor, anchor + '\n    if (budget->live[BUDGET_FEP] > observed_peak) observed_peak = budget->live[BUDGET_FEP];')
        (work / 'vfs.c').write_text('static unsigned observed_peak;\n' + vfs)
        harness = harness.replace('#include "' + str(ROOT / 'lib/sqlite3/os32_sqlite_vfs.c') + '"', '#include "vfs.c"')
    (work / 'ime_dict_host.c').write_text(harness)


def probe_source():
    source = (ROOT / 'kernel/sqlite_engine.c').read_text()
    source = source[source.index('static int probe_close('):source.index('/* Missing/unreadable reserve settings')]
    # Bind only the outer import seam to the real bundled implementation.
    return (source.replace('os32_sqlite_outer_group_', 'os32_sqlite_group_')
            .replace('os32_sqlite_finalize', 'sqlite3_finalize')
            .replace('os32_sqlite_close', 'sqlite3_close')
            .replace('os32_sqlite_prepare_v2', 'sqlite3_prepare_v2')
            .replace('os32_sqlite_step', 'sqlite3_step')
            .replace('os32_sqlite_column_', 'sqlite3_column_'))


def build(work, body, name='test', flags=()):
    source = work / (name + '.c')
    source.write_text('#include "fep_boundary_runtime.h"\n'
        '#define IME_DICT_HOST_MAIN legacy_main\n#include "ime_dict_host.c"\n' + body)
    exe = work / name
    cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    host32.build([cc, '-m32', '-march=i386', '-std=gnu11', '-Os', '-nostdlib', '-fno-builtin',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-D__cdecl=',
        '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare',
        '-Wno-type-limits', '-Wno-missing-field-initializers', '-Wno-maybe-uninitialized',
        '-I' + str(work), '-I' + str(work / 'ime_shim'), '-I' + str(ROOT),
        '-I' + str(ROOT / 'tools/tests/host_arch'), '-I' + str(ROOT / 'tools/tests'),
        '-I' + str(ROOT / 'lib/sqlite3'), *include_flags(ROOT), *flags, str(source),
        *[str(ROOT / p) for p in ('lib/kstring_c.c', 'lib/kstring.c', 'lib/kmath.c', 'lib/divdi3.c')],
        str(work / 'sqlite.o'), '-o', str(exe)], check=True)
    return exe
