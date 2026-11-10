#!/usr/bin/env python3
"""DICT_META C2/O5: actual relocated SQLite, core boot and probe lifetime."""
CHECK = dict(name='fep-core-boot', tier=1, needs=['make:sqlite-module-proto'], budget=12.8,
    covers=['kernel/ime/ime_display_private.h', 'platform/pc98/ime_default_render.h', 'platform/pc98/ime_display.c', 'include/ime_control.h', 'platform/pc98/bootinfo_wire.h', 'platform/pc98/bootinfo_native.h', 'drivers/base/dev.c', 'fs/vfs.c', 'Makefile', 'build/config.mk', 'build/sqlite-proto.mk', 'build/sqlite-module.ld', 'tools/gen_sqlite_imports.py', 'tools/gen_kdebug_imports.py', 'tools/sqlite_module_proto.py', 'tools/mkmod.py', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'tools/prototypes/sqlite_imports.def', 'tools/tests/sqlite_module_proto_tdd.md', 'tools/tests/test_module_image.py', 'tools/tests/module_image_host.c', 'tools/tests/test_module_runtime.py', 'tools/tests/module_runtime_host.c', 'tools/tests/sqlite_module_host.c', 'tools/tests/test_mkmod.py', 'tools/tests/host32.py', 'kernel/module/module_image.c', 'kernel/module/module_image.h', 'kernel/module/module_runtime.c', 'kernel/module/module_runtime.h', 'kernel/sqlite_engine.c', 'kernel/sqlite_engine.h', 'mm/paging.h', 'arch/x86/kernel/isr_handlers.c', 'kernel/sqlite_outer.c', 'mm/memory_boot.h', 'kernel/module/module.h', 'kernel/irq/irq.h', 'mm/pgalloc.h', 'include/memmap.h', 'include/bootinfo.h', 'include/config.h', 'kernel/module/module_schema.h', 'mm/kmalloc.h', 'lib/sqlite3/sqlite3.c', 'lib/sqlite3/sqlite3.h', 'lib/sqlite3/os32_sqlite_vfs.c', 'lib/sqlite3/os32_sqlite_module.c', 'lib/sqlite3/generated/sqlite_facade.h', 'lib/sqlite3/generated/sqlite_imports.h', 'lib/sqlite3/generated/sqlite_kexports.c', 'lib/sqlite3/os32_sqlite_outer.h', 'lib/sqlite3/sqlite_stack.asm', 'lib/sqlite3/os32_sqlite_vfs.h', 'lib/sqlite3/os32_sqlite_config.h', 'lib/kstring.c', 'lib/kstring_c.c', 'lib/kstring.h', 'lib/kmath.c', 'lib/divdi3.c', 'fs/vfs.h', 'include/types.h', 'include/kprintf.h', 'include/sys.h', 'sdk/kapi.json', 'sdk/link/generations.ld', 'sdk/include/os32/os32_unit_stamp.h', 'kdebug/generated/kdebug_imports.json', 'build/module.ld', 'kernel/module/kdebug_imports.def', 'kernel/ime/ime.c', 'kernel/ime/ime.h', 'include/ime_boot.h', 'kernel/ime/ime_dict.c', 'kernel/ime/ime_render.h', 'kernel/console/unicode_file.c', 'kernel/console/unicode_file.h', 'lib/sqlite3/os32_sqlite_test.c', 'include/endian_le.h', 'kapi/kapi_sys.c', 'tools/prototypes/fep_imports.def', 'kernel/ime/fep_module.c', 'kernel/ime/fep_module.h', 'kernel/ime/fep_body.c', 'kernel/ime/ime_dict_info.c', 'kernel/ime/ime_conn.c', 'kernel/ime/ime_store.h', 'kernel/ime/ime_suspend.c', 'build/fep-module.mk', 'kernel/ime/ime_romkana.c', 'platform/pc98/ime_render_tvram.c', 'fs/fd_redirect.h', 'lib/utf8.h', 'include/tvram.h', 'drivers/input/kbd.h', 'drivers/char/serial.h', 'exec/exec.h', 'include/con_sink.h', 'include/redir_access.h', 'build/fep-module.ld', 'build/out/fep/*.o', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'tools/tests/kernel_includes.py', 'tools/tests/test_fep_core_boot.py', 'tools/tests/test_sqlite_module_proto.py'])
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import host32
from test_sqlite_module_proto import function, mkmod, RUNTIME_FLAGS, RUNTIME_INCLUDES
ROOT = Path(__file__).resolve().parents[2]

def engine_seams(source):
    for old, new in (
            ('os32_sqlite_outer_group_open(', 'host_core_group_open('),
            ('ime_boot_connections()', 'host_core_connections()'),
            ('os32_sqlite_outer_boot_check(', 'host_core_boot_check('),
            ('fep_module_abort()', 'host_core_abort()'),
            ('os32_sqlite_close(', 'host_core_close(')):
        source = source.replace(old, new)
    return source


def build(folder, directory):
    """Execute the product module with the full adapter and real stack switch."""
    generated = ROOT / 'lib/sqlite3/generated'
    (folder / 'sys_build_info.inc').write_text(
        function((ROOT / 'kapi/kapi_sys.c').read_text(), 'build_time_copy') + '\n' +
        function((ROOT / 'kapi/kapi_sys.c').read_text(), 'kapi_sys_get_build_info'))
    fep_directory = ROOT / 'build/out/fep'
    fep_elf = mkmod.Elf((fep_directory / 'fep.elf').read_bytes())
    (folder / 'fep_host_offsets.h').write_text(
        '#define HOST_FEP_STATE ' + str(next(sym['value'] for table in fep_elf.symtabs.values() for sym in table if sym['name'] == 'g_ime')) + 'u\n')
    (folder / 'fep_module.c').write_bytes((ROOT / 'kernel/ime/fep_module.c').read_bytes())
    smoke = (ROOT / 'lib/sqlite3/os32_sqlite_test.c').read_text()
    (folder / 'sqlite_smoke.c').write_text(smoke)
    for name in ('sqlite_engine', 'sqlite_outer'):
        source = (ROOT / ('kernel/ime' if name in ('ime', 'ime_dict') else 'kernel') / (name + '.c')).read_text()
        if name == 'sqlite_engine':
            source = engine_seams(source)
            source = source.replace('#include "../lib/sqlite3/generated/sqlite_facade.h"',
                '#include "../lib/sqlite3/generated/sqlite_facade.h"\n'
                'extern int host_core_group_open(const Os32SqliteGroup *, const char *, int, sqlite3 **);\n'
                'extern u32 host_core_connections(void);\n'
                'extern int host_core_boot_check(int, int), host_core_abort(void), host_core_close(sqlite3 *);')
        (folder / (name + '.c')).write_text(source.replace(
            '../lib/sqlite3/generated/', str(generated) + '/'))
    (folder / 'io.h').write_text(
        'extern unsigned long host_flags;\n'
        'void host_stop(void);\n'
        'static inline int _irq_enabled(void) { return (host_flags & 512UL) != 0; }\n'
        'static inline void _disable(void) { host_flags &= ~512UL; }\n'
        'static inline void _stop(void) { host_stop(); }\n'
        'static inline unsigned long irq_save(void) { unsigned long f=host_flags; '
        'host_flags &= ~512UL; return f ^ 512UL; }\n'
        'static inline void irq_restore(unsigned long f) { host_flags=f ^ 512UL; }\n')
    handlers = (ROOT / 'arch/x86/kernel/isr_handlers.c').read_text()
    start = handlers.index('void sqlite_engine_fault(')
    (folder / 'sqlite_fault.inc').write_text(
        '#include "io.h"\n#include "pc98.h"\n' + handlers[start:handlers.index('\n}', start) + 2])
    # Real contracts, with unneeded disk/heap imports bound to a failing probe.
    # :memory: still executes the actual module's VFS registry and MEMSYS5.
    exports = (generated / 'sqlite_kexports.c').read_text()
    end = exports.index('};', exports.index('const u32 sqlite_kexports[]')) + 2
    metadata = json.loads((directory / 'sqlite_imports.json').read_text())
    used = {'kprintf', 'sys_time', 'ktime_ticks', 'vfs_count_sqlite', 'vfs_quarantine_sqlite'}
    (folder / 'sqlite_kexports.c').write_text(
        '#include "sqlite_imports.h"\n#include "module_image.h"\n'
        'extern void sqlite_forbidden_import(void);\n'
        'const u32 sqlite_kexports[] = {\n' + ''.join(
            '    (u32)(uptr)' + (row['name'] if row['name'] in used else 'sqlite_forbidden_import') + ',\n'
            for row in metadata['imports']) + '};\n' + exports[end:])
    fep_metadata = json.loads((fep_directory / 'fep_imports.json').read_text())
    fep_exports = (fep_directory / 'fep_kexports.c').read_text()
    end = fep_exports.index('};', fep_exports.index('const u32 fep_kexports[]')) + 2
    used = {'kmemcpy', 'kmemset', 'kstrncpy', 'kstrlen', 'kprintf', 'kmalloc',
            'kfree', 'kmalloc_block_size', 'serial_puts', 'sqlite_engine_available',
            'con_sink_is_enabled', 'kbd_trygetkey', 'kbd_trygetchar', 'kbd_getkey_wait',
            'ring3_call_from_user', 'res_owner_get', 'ring3_abort_check',
            'tvram_set_scroll_reserve', 'vfs_stat', 'vfs_fd_is_stale', 'kstrcmp', 'kstrncmp'}
    wrappers = {'os32_sqlite_outer_db_fd': 'host_fep_db_fd', 'os32_sqlite_outer_group_open': 'host_fep_group_open',
                'os32_sqlite_outer_group_acquire': 'host_fep_group_acquire',
                'os32_sqlite_prepare_v2': 'host_fep_prepare'}
    (folder / 'fep_kexports.c').write_text(
        '#include "fep_imports.h"\n#include "module_image.h"\n'
        'extern void sqlite_forbidden_import(void);\n'
        'extern int host_fep_group_open(const Os32SqliteGroup *, const char *, int, sqlite3 **);\n'
        'extern int host_fep_group_acquire(int, int, Os32SqliteGroup *);\n'
        'extern int host_fep_db_fd(void *);\n'
        'extern int host_fep_prepare(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);\n'
        'const u32 fep_kexports[] = {\n' + ''.join(
            '    (u32)(uptr)' + wrappers.get(row['name'], row['name'] if
                row['name'] in used or row['name'].startswith(('os32_sqlite_', 'fep_'))
                else 'sqlite_forbidden_import') + ',\n' for row in fep_metadata['imports'])
        + '};\n' + fep_exports[end:])
    obj = folder / 'sqlite_stack.o'
    subprocess.run(['nasm', '-f', 'elf32', ROOT / 'lib/sqlite3/sqlite_stack.asm', '-o', obj], check=True)
    exe = folder / 'sqlite-module'
    command = ['gcc', *RUNTIME_FLAGS, '-Werror=vla', '-I' + str(folder), *RUNTIME_INCLUDES,
                    '-I' + str(ROOT / 'lib/sqlite3'), '-I' + str(generated),
                    '-I' + str(fep_directory), '-I' + str(ROOT / 'drivers'),
                    '-nostdlib', '-static', '-no-pie',
                    ROOT / 'tools/tests/sqlite_module_host.c',
                    folder / 'sqlite_engine.c', folder / 'sqlite_outer.c',
                    folder / 'sqlite_kexports.c', folder / 'fep_kexports.c', folder / 'fep_module.c',
                    ROOT / 'kernel/module/module_runtime.c',
                    ROOT / 'kernel/module/module_image.c', ROOT / 'kernel/console/unicode_file.c',
                    folder / 'sqlite_smoke.c', obj, '-o', exe]
    subprocess.run(command, check=True)
    return exe, command

MUTATIONS = [
    ('settings-absent-db', 'rc = vfs_stat(SYS_SETTINGS_DB, &st) == VFS_ERR_NOTFOUND ? 0 : OS32_ERR_IO;',
     'rc = vfs_stat(SYS_SETTINGS_DB, &st) == VFS_ERR_NOTFOUND ? OS32_ERR_IO : OS32_ERR_IO;', 89),
    ('settings-absent-table', 'if (step == SQLITE_ROW) has_settings = 1;\n            else if (step != SQLITE_DONE) rc = OS32_ERR_IO;',
     'if (step == SQLITE_ROW) has_settings = 1;\n            else rc = OS32_ERR_IO;', 90),
    ('future-header', 'if (version > FEP_DICT_FORMAT_VERSION) return OS32_ERR_INVAL;',
     'if (version > FEP_DICT_FORMAT_VERSION) version = 1;', 66),
    ('header-magic', 'if (memcmp(header, "SQLite format 3", 16) ||',
     'if (0 ||', 67),
    ('meta-format', 'case 0: if (number != version) rc = OS32_ERR_INVAL; break;',
     'case 0: break;', 70),
    ('uint-overflow', "value > (0xffffffffu - (s[i] - '0')) / 10u)", '0)', 72),
    ('cache-budget', 'FEP_USER_CACHE_PAGES > meta.reserve_kb - meta.cache_pages)',
     '0)', 77),
    ('settings-priority', 'if (!reserve_present) {', 'if (1) {', 62),
    ('zero-is-present', '*present = 1;', '*present = kb != 0;', 63),
    ('settings-minimum', '*reserve = OS32_SQLITE_FEP_RESERVE_MIN;', '*reserve = -1;', 63),
    ('boot-live-count', 'os32_sqlite_outer_boot_check(ime_boot_connections(), reserve)',
     'os32_sqlite_outer_boot_check(1, reserve)', 85),
    ('probe-close', 'int close_rc = probe_close(&group, db, stmt);',
     'int close_rc = db ? 0 : probe_close(&group, db, stmt);', 61),
    ('path-read-rc', '    boot_user_rc = rc > 0 ? OS32_ERR_IO : rc;', '    boot_user_rc = 0;', 80),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    directory = ROOT / 'build/out/sqlite-proto'
    pair = mkmod.pair_crc((ROOT / 'build/out/vmkernel.lz4').read_bytes())
    image = (directory / 'sqlite.bin').read_bytes()
    fep = (ROOT / 'build/out/fep.mod').read_bytes()
    with tempfile.TemporaryDirectory(prefix='fep-core-') as td:
        folder = Path(td)
        exe, command = build(folder, directory)
        def run_case(mode):
            payload = struct.pack('<4I', mode, len(image), pair, 0) + image
            payload += struct.pack('<I', len(fep)) + fep
            return host32.run([str(exe)], input=payload, capture_output=True)
        for mode in range(61, 93):
            if mode == 79:
                continue
            result = run_case(mode)
            assert result.returncode == 0 and b'GREEN core' in result.stdout, (mode, result)
            print(f'PASS C2 case={mode}: {result.stdout.decode().strip()}', flush=True)
        if args.mutate:
            path = folder / 'sqlite_engine.c'
            product = path.read_text()
            for name, old, new, mode in MUTATIONS:
                old, new = engine_seams(old), engine_seams(new)
                assert product.count(old) == 1, (name, product.count(old))
                path.write_text(product.replace(old, new))
                subprocess.run(command, check=True, capture_output=True)
                result = run_case(mode)
                assert result.returncode == 1 and b'FAIL ' in result.stdout, (name, result)
                print(f'RED {name}: compiled runtime case={mode}: {result.stdout.decode().strip()}', flush=True)
            path.write_text(product)
    return 0


def run(ctx):
    for runner in ctx.runners:
        cmd = [sys.executable, '-B', 'tools/tests/test_fep_core_boot.py']
        if ctx.mutate:
            cmd.append('--mutate')
        rc = subprocess.call(cmd, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
