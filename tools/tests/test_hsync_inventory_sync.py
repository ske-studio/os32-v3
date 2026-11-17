#!/usr/bin/env python3
"""Stage D: production hsync main, inventory acquisition/comparison/publication."""
CHECK = dict(name='hsync-inventory-sync', tier=1, needs=[], budget=60,
    covers=['userland/system/hsync.c', 'userland/system/hsync_*.inc',
            'tools/tests/test_hsync_inventory_sync.py',
            'tools/tests/hsync_inventory_sync_host.c', 'tools/tests/hsync_h2_host.c',
            'tools/tests/hsync_shell_fixture.h', 'tools/tests/fep_boundary_runtime.h',
            'tools/tests/host32.py', 'sdk/include/os32/**', 'include/**',
            'userland/lib/**', 'lib/kstring*', 'lib/crc32_core.inc', 'exec/os32x_hdr.c'],
    basis=['HSYNC_MANIFEST_DESIGN §4', 'HSYNC_MANIFEST_DESIGN §6',
           'HSYNC_MANIFEST_DESIGN §8'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'userland/system/hsync.c'
MUTANTS = [
    ('joined-path-fatal', 'return 0; /* Use legacy per-entry path failures. */',
     'return OS32_ERR_NAMETOOLONG;'),
    ('other-reason', 'if (entry && entry[0] == 3) { fail_file(dst_path, HR_TYPE_UNKNOWN, 0);',
     'if (entry && entry[0] == 3) { fail_file(dst_path, HR_TYPE, 0);'),
    ('verify-mtime', 'if (!g_verify && mtime_same) {', 'if (mtime_same) {'),
    ('mtime-reads', '/* サイズも日時も同じ。**1 バイトも読まない** — ここが速さの源 */',
     'u32 c, b; (void)hsi_local_crc(dst_path, size, &c, &b);'),
    ('metadata-copies', '            need = 0;', '            need = 1;'),
    ('remote-compare', 'cmp = crc != hsi_get32(entry + 12);',
     'cmp = compare_files(src_path, dst_path, size);'),
    ('copy-crc', 'if (entry && (total != hsi_get32(entry + 4) ||',
     'if (0 && entry && (total != hsi_get32(entry + 4) ||'),
    ('source-stat', '        rc = 0;\n    } else {\n        rc = api->sys_stat(src_path, &ss);',
     '        rc = api->sys_stat(src_path, &ss);\n    } else {\n        rc = api->sys_stat(src_path, &ss);'),
    ('legacy-list', 'g_inventory_active ? inventory_list(src_dir, &fl) : os32_ls(src_dir, ls_cb, &fl)',
     'g_inventory_active ? (inventory_list(src_dir, &fl), os32_ls(src_dir, ls_cb, &fl)) : os32_ls(src_dir, ls_cb, &fl)'),
    ('fallback-fatal', 'if (rc == HSI_FALLBACK) return 0;',
     'if (rc == HSI_FALLBACK) return OS32_ERR_IO;'),
    ('communication-fallback', 'if (rc != 0) return rc;\n    for (i = 0; i < g_inventory.count;',
     'if (rc != 0) return 0;\n    for (i = 0; i < g_inventory.count;'),
    ('local-IO', 'if (rc != 0) { fail_file(dst_path, HR_IO, rc); return; }\n            g_local_crc_files++;',
     'if (rc != 0) { return; }\n            g_local_crc_files++;'),
    ('protection-after-crc', '        /* ---- 要素数の検査 (B2) ----',
     '        if (inventory_entry(src_path)) { u32 c, b; '
     '(void)hsi_local_crc(dst_path, 32, &c, &b); }\n'
     '        /* ---- 要素数の検査 (B2) ----'),
    ('boot-backup-before-compare', '    /* 宛先 */\n    rc = api->sys_stat(dst_path, &ds);',
     '    if (str_cmp(hs_rel(dst_path), HS_KERNEL_PATH) == 0 && '
     'api->sys_stat(dst_path, &ds) == 0 && kernel_backup(dst_path, &ds) != 0) return;\n'
     '    /* 宛先 */\n    rc = api->sys_stat(dst_path, &ds);'),
    ('skipped-display', 'g_inventory.count, g_inventory.skipped);',
     'g_inventory.count, 0u);'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / PRODUCT).read_text()
    with tempfile.TemporaryDirectory(prefix='hsync-inventory-sync-') as td:
        work = Path(td)
        # The kernel-backup diagnostic uses %08X. Consume it just like %x;
        # the shared minimal fixture otherwise misaligns subsequent varargs.
        runtime = (ROOT / 'tools/tests/fep_boundary_runtime.h').read_text()
        (work / 'fep_boundary_runtime.h').write_text(
            runtime.replace("*fmt == 'x'", "(*fmt == 'x' || *fmt == 'X')"))
        for header in ('stdio.h', 'stdlib.h', 'string.h'):
            (work / header).write_text('#include "fep_boundary_runtime.h"\n')
        fixture = (ROOT / 'tools/tests/hsync_h2_host.c').read_text()
        fixture = fixture.replace('#define FS_MAX_NODES 96', '#define FS_MAX_NODES 384')
        fixture = fixture.replace('int main(void)', 'int hsync_h2_fixture_main(void)')
        fixture = fixture.replace('../../userland/system/hsync.c', str(work / 'hsync.c'))
        fixture = fixture.replace('    u8 *p = (u8 *)realloc(nd->data, newcap);',
            '    u8 *p = (u8 *)malloc(newcap);\n        if (p && nd->data) memcpy(p, nd->data, nd->cap);')
        fixture = fixture.replace('vsnprintf(line, sizeof(line), fmt, ap)',
                                  'fixture_format(line, sizeof(line), fmt, ap)')
        (work / 'hsync_h2_host.c').write_text(fixture)
        (work / 'test.c').write_text((ROOT / 'tools/tests/hsync_inventory_sync_host.c').read_text())
        for inc in (ROOT / 'userland/system').glob('hsync_*.inc'):
            (work / inc.name).write_text(inc.read_text())
        # Include mutations also run through production main and the legacy fixture.
        subprocess.run(['grep', '-nF', '#include "hsync_inventory.inc"', str(ROOT / PRODUCT)], check=True)
        subprocess.run(['grep', '-nF', 'static int hsi_acquire(', str(work / 'hsync_inventory.inc')], check=True)
        def run(source):
            (work / 'hsync.c').write_text(source)
            exe = work / 'test'
            host32.build([str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'),
                '-m32', '-march=i386', '-std=gnu11', '-Os', '-nostdlib',
                '-fno-builtin', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-D__cdecl=',
                '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare',
                '-I' + str(work), *['-I' + str(ROOT / p) for p in
                 ('tools/tests', '.', 'include', 'arch/x86', 'sdk/include',
                  'sdk/include/os32', 'userland/lib', 'userland/system', 'lib')],
                str(work / 'test.c'), str(ROOT / 'lib/kstring.c'),
                str(ROOT / 'lib/kstring_c.c'), '-o', str(exe)], check=True)
            result = host32.run([str(exe)], capture_output=True, text=True, timeout=60)
            print(result.stdout + result.stderr, end='', flush=True)
            return result.returncode
        assert run(original) == 0, 'normal control failed'
        print('CONTROL GREEN', flush=True)
        if args.mutate:
            for name, old, new in MUTANTS:
                assert original.count(old) == 1, (name, original.count(old))
                rc = run(original.replace(old, new))
                assert rc > 0, (name, 'runtime RED required', rc)
                assert run(original) == 0, (name, 'restored control failed')
                print('MUTATE', name, 'runtime RED; restored GREEN', flush=True)
            inc_path = work / 'hsync_inventory.inc'
            inc = inc_path.read_text()
            for old, new in [
                ('if (component >= NAME_CAP || depth >= MAX_DEPTH) return HSI_FALLBACK;',
                 'if (component >= NAME_CAP || depth >= MAX_DEPTH) return OS32_ERR_IO;'),
                ('if (component >= NAME_CAP || n >= OS32_MAX_PATH) return HSI_FALLBACK;',
                 'if (component >= NAME_CAP || n >= OS32_MAX_PATH) return OS32_ERR_IO;'),
                ('if (++children[depth] > MAX_FILES) return HSI_FALLBACK;',
                 'if (++children[depth] > MAX_FILES) return OS32_ERR_IO;'),
            ]:
                assert inc.count(old) == 1, old
                inc_path.write_text(inc.replace(old, new))
                rc = run(original)
                assert rc > 0, (old, 'runtime RED required', rc)
                inc_path.write_text(inc)
                assert run(original) == 0, 'restored include failed'
                print('MUTATE', old, 'runtime RED; restored GREEN', flush=True)
            assert run(original) == 0, 'restored control failed'
            print('RESTORED GREEN', flush=True)


def run(ctx):
    return subprocess.call(['python3', '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
