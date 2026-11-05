#!/usr/bin/env python3
"""Trident 3b route: complete reference traces through the shared 3a fake.

Oracle and license provenance live in trident_route_host.c / route_ra266.c.
No local reference checkout or 32-bit libc is needed by CI. Production
regs/lifecycle/module assembly belongs to their subsequent integration test.
"""
CHECK = dict(name='trident-route', tier=1, needs=[], budget=30,
    covers=['drivers/video/trident/route_ra266.c',
            'drivers/video/trident/trident.h',
            'tools/tests/trident_route_host.c',
            'tools/tests/trident_device_fake.c', 'tools/tests/trident_device_fake.h',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'build/config.mk', 'include/types.h', 'include/memmap.h', 'gfx/palette.h'])

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'drivers/video/trident/route_ra266.c'
HOST = 'tools/tests/trident_route_host.c'
FAKE = 'tools/tests/trident_device_fake.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
# Every tuple has a unique literal product anchor; compilation failure is
# never a mutation success. All mutants must produce an explicit runtime RED.
MUTATIONS = (
    ('drop-rc', 'if (route_rc) return route_rc;',
     'if (route_rc) return TRI_E_ACCESS;'),
    ('outw-to-outb',
     'return trident_indexed_write(d, port, index, (value & mask) | bits);',
     'return trident_write8(d, TRI_SPACE_REGS, port, (value & mask) | bits);'),
    ('enable-port-mode', 'trident_port8(d, TRI_PORT_MODE, 0x0E)',
     'trident_port8(d, TRI_PORT_MODE, 0x0F)'),
    ('enable-ff-order',
     'ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x8F));\n    ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x06));',
     'ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x06));\n    ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x8F));'),
    ('enable-protect',
     'ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x06));\n    ROUTE_TRY(trident_protect(d, d->saved.vga_base, true));',
     'ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x06));\n    ROUTE_TRY(0);'),
    ('enable-unprotect-base',
     'ROUTE_TRY(trident_protect(d, d->saved.vga_base, false));\n    return trident_port8(d, TRI_PORT_ROUTE, 0x02);',
     'ROUTE_TRY(trident_protect(d, TRI_CR_COLOR, false));\n    return trident_port8(d, TRI_PORT_ROUTE, 0x02);'),
    ('enable-fac', 'return trident_port8(d, TRI_PORT_ROUTE, 0x02);',
     'return trident_port8(d, TRI_PORT_ROUTE, 0x00);'),
    ('disable-fac-order',
     'ROUTE_TRY(trident_port8(d, TRI_PORT_ROUTE, 0x00));\n    ROUTE_TRY(trident_protect(d, d->saved.vga_base, true));',
     'ROUTE_TRY(trident_protect(d, d->saved.vga_base, true));\n    ROUTE_TRY(trident_port8(d, TRI_PORT_ROUTE, 0x00));'),
    ('disable-unprotect',
     'ROUTE_TRY(trident_protect(d, d->saved.vga_base, false));\n\n    /* D:769-772',
     'ROUTE_TRY(0);\n\n    /* D:769-772'),
    ('disable-ff-order',
     'ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x8E));\n    ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x06));',
     'ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x06));\n    ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x8E));'),
    ('disable-port-mode', 'return trident_port8(d, TRI_PORT_MODE, 0x0F);',
     'return trident_port8(d, TRI_PORT_MODE, 0x0E);'),
)
# Deleting each distinct RMW detects omissions even when a later RMW hides
# the change in the final value. Addresses/masks/bits are frozen from D:706-765.
for name, call in (
    ('enable-cr23', 'route_update(d, TRI_CR_COLOR, 0x23, 0xDF, 0x00)'),
    ('enable-cr29', 'route_update(d, TRI_CR_COLOR, 0x29, 0xFF, 0x04)'),
    ('enable-dac06', 'trident_ext_dac_update(d, 0xFF, 0x06)'),
    ('enable-dac08', 'trident_ext_dac_update(d, 0xFF, 0x08)'),
    ('enable-gr23', 'route_update(d, TRI_GR_INDEX, 0x23, 0xFC, 0x00)'),
    ('enable-dac01', 'trident_ext_dac_update(d, 0xFF, 0x01)'),
    ('enable-sr01', 'route_update(d, TRI_SR_INDEX, 0x01, 0xEF, 0x00)'),
    ('disable-sr01', 'route_update(d, TRI_SR_INDEX, 0x01, 0xFF, 0x10)'),
    ('disable-dac-fe', 'trident_ext_dac_update(d, 0xFE, 0x00)'),
    ('disable-gr23', 'route_update(d, TRI_GR_INDEX, 0x23, 0xFC, 0x01)'),
    ('disable-dac-fd', 'trident_ext_dac_update(d, 0xFD, 0x00)'),
    ('disable-dac-cf', 'trident_ext_dac_update(d, 0xCF, 0x00)'),
    ('disable-dac-f7', 'trident_ext_dac_update(d, 0xF7, 0x00)'),
    ('disable-dac-fb', 'trident_ext_dac_update(d, 0xFB, 0x00)'),
    ('disable-cr29', 'route_update(d, TRI_CR_COLOR, 0x29, 0xFB, 0x00)'),
    ('disable-cr23', 'route_update(d, TRI_CR_COLOR, 0x23, 0xFF, 0x20)'),
):
    MUTATIONS += ((name, f'ROUTE_TRY({call});', 'ROUTE_TRY(0);'),)


def build(work, source):
    exe = work / 'route'
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT),
                    '-I' + str(ROOT / 'drivers/video/trident'), *include_flags(ROOT),
                    '-nostdlib', '-static', '-no-pie', str(source),
                    str(ROOT / HOST), str(ROOT / FAKE), '-o', str(exe)],
                   cwd=ROOT, check=True)
    return exe


def run_exe(exe, runners, mutant=None):
    for runner in runners:
        result = host32.run([str(exe)], runner=runner, capture_output=True)
        if mutant is None:
            assert result.returncode == 0 and result.stdout.startswith(b'GREEN route:'), result
            print(f'PASS ({runner}): {result.stdout.decode().strip()}', flush=True)
        else:
            assert result.returncode == 1 and result.stdout.startswith(b'RED '), (mutant, result)
            print(f'MUTATE RED {mutant} ({runner}): {result.stdout.decode().strip()}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    assert runners, 'no host32 runners'
    with tempfile.TemporaryDirectory(prefix='trident-route-') as tmp:
        work = Path(tmp)
        # Compile the real production translation unit with its real headers
        # and cross compiler; do not substitute a toy implementation of route.
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), *FLAGS, '-I' + str(ROOT), *include_flags(ROOT),
                        '-c', str(ROOT / PRODUCT), '-o', str(work / 'route.o')], check=True)
        print('PASS production route TU (i386-elf-gcc)', flush=True)
        run_exe(build(work, ROOT / PRODUCT), runners)
        if args.mutate:
            original = (ROOT / PRODUCT).read_text()
            source = work / 'mutant.c'
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, original.count(old))
                source.write_text(original.replace(old, new))
                run_exe(build(work, source), runners, name)
    return 0


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_route.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    raise SystemExit(main())
