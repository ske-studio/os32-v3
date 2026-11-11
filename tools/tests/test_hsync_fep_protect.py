#!/usr/bin/env python3
"""DICT_META F/O4: lexical rules, real entity scan, aliases and 24-slot boundary."""
CHECK = dict(name='hsync-fep-protect', tier=1, needs=[], budget=3.4,
    covers=['exec/os32x_hdr.c', 'userland/system/hsync_protect.inc', 'userland/system/hsync.c',
            'tools/tests/hsync_fep_protect_host.c', 'tools/tests/host32.py',
            'sdk/include/os32/os32_kapi_shared.h'])
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'userland/system/hsync_protect.inc'
SCAN = 'userland/system/hsync.c'
# (name, file, unique old, new). Compilation must succeed; RED is runtime only.
MUTANTS = [
    ('all-db', PRODUCT, 'if (hsp_fep_all_db) {', 'if (0 && hsp_fep_all_db) {'),
    ('all-db-case', PRODUCT, 'hsp_fep_prefix(name + i, ".db")',
     'hsp_ieq(name + i, ".db")'),
    ('full-name', SCAN, 'g_prot_name[HS_MAX_PROT][OS32_MAX_PATH]',
     'g_prot_name[HS_MAX_PROT][64]'),

    ('setter-success', PRODUCT,
     "hsp_fep_user_path[i] = '\\0';\n    return 0;",
     "hsp_fep_user_path[i] = '\\0';\n    return 1;"),
    ('default-prefix', PRODUCT, 'if (hsp_fep_prefix(name, HSP_FEP_USER_PATH',
     'if (0 && hsp_fep_prefix(name, HSP_FEP_USER_PATH'),
    ('custom-prefix', PRODUCT, 'if (hsp_fep_user_path[0] &&',
     'if (0 && hsp_fep_user_path[0] &&'),
    ('suffix', PRODUCT, "    return 1;\n}\n\n/* ベース名", 
     "    return *name == '\\0';\n}\n\n/* ベース名"),
    ('direct-child', PRODUCT, "if (norm[i] == '/') return OS32_ERR_INVAL;",
     "if (norm[i] == '/') break;"),
    ('absolute', PRODUCT, "if (!absolute_path || absolute_path[0] != '/') return OS32_ERR_INVAL;",
     'if (!absolute_path) return OS32_ERR_INVAL;'),
    ('journal-cap', PRODUCT,
     'i + (int)sizeof(HSP_FEP_JOURNAL_SUFFIX) > HSP_FEP_PATH_CAP',
     'i + (int)sizeof(HSP_FEP_JOURNAL_SUFFIX) > HSP_FEP_PATH_CAP + 1'),
    ('owned-copy', PRODUCT, 'hsp_fep_user_path[i] = norm[i];',
     "hsp_fep_user_path[i] = 'x';"),
    ('ancestor', PRODUCT, 'if (hsp_name_protected(prefix)) return 1;',
     'if (0 && hsp_name_protected(prefix)) return 1;'),
    ('overflow', SCAN, '        g_prot_overflow++;', '        g_prot_overflow += 0;'),
    ('alias-device', SCAN, 'here.st_dev == g_prot_dev[i] &&', '1 &&'),
    ('alias-inode', SCAN, 'here.st_ino == g_prot_ino[i]', '1'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    sources = {p: (ROOT / p).read_text() for p in (PRODUCT, SCAN)}
    harness = (ROOT / 'tools/tests/hsync_fep_protect_host.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hsync-fep-') as tmp:
        work = Path(tmp)
        def build_run(inputs):
            scan = inputs[SCAN]
            scan = scan[scan.index('#define HS_MAX_PROT '):
                        scan.index('/* コピー / mkdir の**直前**')]
            unit = harness.replace('#include "userland/system/hsync_protect.inc"',
                                   inputs[PRODUCT]).replace('/* HSP_ENTITY_SCAN */', scan)
            (work / 'test.c').write_text(unit)
            exe = work / 'test'
            subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                            '-Wall', '-Wextra', '-Werror', '-ffreestanding',
                            '-fno-pie', '-fno-stack-protector', '-nostdlib',
                            '-static', '-no-pie', '-I' + str(ROOT / 'sdk/include'),
                            str(work / 'test.c'), '-o', str(exe)], check=True)
            return host32.run([str(exe)], cwd=ROOT).returncode
        rc = build_run(sources)
        if rc:
            raise RuntimeError(f'normal runtime failed rc={rc}')
        print('PASS default/custom/suffix/ancestor/path limits/entity aliases/24+1; '
              'runner=' + host32.selected_runner(), flush=True)
        if args.mutate:
            for name, path, old, new in MUTANTS:
                if sources[path].count(old) != 1:
                    raise RuntimeError(f'{name}: anchor not unique')
                changed = dict(sources)
                changed[path] = changed[path].replace(old, new)
                rc = build_run(changed)
                if rc <= 0:
                    raise RuntimeError(f'{name}: runtime RED required rc={rc}')
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_hsync_fep_protect.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
