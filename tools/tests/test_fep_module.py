#!/usr/bin/env python3
"""Freestanding ILP32 production FEP facade validation and boot count relay."""
CHECK = dict(name='fep-module', tier=1, needs=[], budget=3.3,
    covers=['kernel/ime/fep_module.c', 'kernel/ime/fep_module.h',
            'kernel/ime/fep_body.c', 'kernel/ime/ime.h', 'kernel/ime/ime_store.h',
            'tools/tests/fep_module_host.c',
            'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk',
            'lib/kstring.c', 'lib/kstring_c.c'])
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'kernel/ime/fep_module.c'
OLD = 'return candidate_valid ? candidate.boot_connections() : stub_api.boot_connections();'
NEW = 'return candidate_valid ? 0 : stub_api.boot_connections();'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fep-module-') as tmp:
        work = Path(tmp)
        # Import metadata belongs to the loader suite; no load is invoked here.
        (work / 'fep_imports.h').write_text(
            '#define FEP_IMPORT_COUNT 0u\n#define FEP_MODULE_ABI_GEN 1u\n')
        flags = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                 '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                 '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra',
                 '-Werror', '-I' + str(ROOT), '-I' + str(ROOT / 'kernel/ime'),
                 '-I' + str(work),
                 *include_flags(ROOT)]
        body = work / 'body.o'
        subprocess.run([*flags, '-Dime_boot_connections=body_boot_connections',
                        '-c', str(ROOT / 'kernel/ime/fep_body.c'), '-o', str(body)], check=True)
        product = (ROOT / PRODUCT).read_text()
        harness = (ROOT / 'tools/tests/fep_module_host.c').read_text()
        for label, code in [('normal', product)] + ([(
                'boot-relay-zero', product.replace(OLD, NEW))] if args.mutate else []):
            if label != 'normal' and product.count(OLD) != 1:
                raise RuntimeError('mutation anchor not unique')
            unit = work / 'module.c'
            unit.write_text(harness.replace('#include "kernel/ime/fep_module.c"', code))
            exe = work / label
            subprocess.run([*flags, '-nostdlib', '-static', '-no-pie',
                            '-Wl,--gc-sections', str(unit), str(body),
                            str(ROOT / 'lib/kstring_c.c'), '-o', str(exe)], check=True)
            rc = host32.run([str(exe)], cwd=ROOT).returncode
            if label == 'normal' and rc != 0:
                raise RuntimeError(f'normal rc={rc}')
            if label != 'normal' and rc != 4:
                raise RuntimeError(f'runtime relay RED required, rc={rc}')
            print(f'{"PASS" if label == "normal" else "RED"} {label}: runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_fep_module.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
