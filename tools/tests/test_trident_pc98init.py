#!/usr/bin/env python3
"""3b PC98 Init: full independent MMIO trace and runtime mutations, no libc.

The C oracle is transcribed from MIT trident_driver-1.4.0.c:616-692 and
userland/tests/hw/trident_mode.hwb:499-577; no external reference file is
needed in CI. regs.c link doubles are stateless adapters to the shared fake.
Production regs_bind/module wiring remains the wiring-stage acceptance test.
"""
CHECK = dict(name='trident-pc98init', tier=1, needs=[], budget=10,
    covers=['drivers/video/trident/pc98_init.c',
            'drivers/video/trident/trident.h',
            'tools/tests/trident_pc98init_host.c',
            'tools/tests/trident_device_fake.c',
            'tools/tests/trident_device_fake.h',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'build/config.mk', 'include/types.h', 'include/memmap.h',
            'gfx/palette.h', 'include/pegc.h',
            'userland/tests/hw/trident_mode.hwb'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'drivers/video/trident/pc98_init.c'
HOST = 'tools/tests/trident_pc98init_host.c'
FAKE = 'tools/tests/trident_device_fake.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
# Every anchor is unique; compiler failure or signal is never mutation RED.
MUTATIONS = (
    ('M_SAVED_BASE',
     'trident_crtc_lock(d, d->saved.vga_base, false)',
     'trident_crtc_lock(d, TRI_CR_COLOR, false)'),
    ('M_INIT_FIFO',
     'TRY(trident_write16(d, TRI_SPACE_REGS, TRI_CR_COLOR, 0x3820));',
     'TRY(0);'),
    ('M_OUTW_TO_OUTB',
     'trident_write16(d, TRI_SPACE_REGS, TRI_CR_COLOR, 0x0E30)',
     'trident_write8(d, TRI_SPACE_REGS, TRI_CR_COLOR, 0x30)'),
    ('M_INIT_PROTECT',
     'trident_protect(d, d->saved.vga_base, true)',
     'trident_protect(d, d->saved.vga_base, false)'),
    ('M_EXIT_PROTECT', 'trident_protect(d, d->saved.vga_base, false)',
     'trident_protect(d, d->saved.vga_base, true)'),
    ('M_SKIP_PC98', 'u8 temp, config;', 'u8 temp, config; return TRI_OK;'),
    ('M_SR0C_ALT_DELETE',
     'TRY(trident_write8(d, TRI_SPACE_REGS, TRI_CONFIG_ALT, 0x10));',
     'TRY(0);'),
    ('M_SR0C_BRANCH', 'if (config & 0x10)', 'if (config & 0x01)'),
    ('M_D636_OUTB_TO_OUTW',
     '/* D:637 (design D:636) is OUTB, discarding temp\'s upper byte. */\n'
     '        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_SR_INDEX, (u8)(0x0E | (temp << 8))));',
     'TRY(trident_write16(d, TRI_SPACE_REGS, TRI_SR_INDEX, 0x0E | (temp << 8)));'),
    ('M_D643_OUTB_TO_OUTW',
     '/* D:644 (design D:643) is OUTB; H:543-546 retains this alternate path. */\n'
     '        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_SR_INDEX, (u8)(0x0E | (temp << 8))));',
     'TRY(trident_write16(d, TRI_SPACE_REGS, TRI_SR_INDEX, 0x0E | (temp << 8)));'),
    ('M_SR0E_CONFIG', '0x0E, temp | 0x20)', '0x0E, temp)'),
    ('M_SR0E_UNLOCK', '0x0E, temp | 0x80)', '0x0E, temp)'),
    ('M_SR0E_LOCK', '0x0E, temp & 0x7F)', '0x0E, temp)'),
    ('M_MCLK', 'TRI_MCLK_LO, 0xAF)', 'TRI_MCLK_LO, 0xA7)'),
    ('M_COLOR_BASE', 'trident_ar_write(d, TRI_CR_COLOR, 0x10, 0x41)',
     'trident_ar_write(d, TRI_CR_MONO, 0x10, 0x41)'),
    ('M_DROP_RC', 'if (rc) return rc;', 'if (rc) return TRI_OK;'),
    ('M_INIT_DONE_OWNER', 'return TRI_OK;', 'd->init_done = true; return TRI_OK;'),
)


def compile_host(work, source):
    exe = work / 'pc98init'
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT), *include_flags(ROOT),
                    '-I' + str(ROOT / 'drivers/video/trident'),
                    '-nostdlib', '-static', '-no-pie', str(source),
                    str(ROOT / HOST), str(ROOT / FAKE), '-o', str(exe)],
                   cwd=ROOT, check=True)
    return exe


def execute(exe, runners, red=False):
    for runner in runners:
        result = host32.run([str(exe)], runner=runner, capture_output=True)
        if red:
            assert result.returncode == 1 and result.stdout.startswith(b'RED pc98init '), result
        else:
            assert result.returncode == 0 and result.stdout == b'GREEN pc98init\n', result
    return result.stdout.decode().strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    assert runners, 'no host32 runners'
    with tempfile.TemporaryDirectory(prefix='trident-pc98init-') as tmp:
        work = Path(tmp)
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        # make all does not wire this new object until 3d; compile the real TU
        # with the production compiler separately in this component stage.
        subprocess.run([str(cross), *FLAGS, '-I' + str(ROOT), *include_flags(ROOT),
                        '-c', str(ROOT / PRODUCT), '-o', str(work / 'pc98_init.o')],
                       check=True)
        print('PASS production TU cross compile', flush=True)
        exe = compile_host(work, ROOT / PRODUCT)
        print('PASS full trace: all SR0C bytes, mono/color, RMW bits, BAR0 gate, '
              'failure rc at every access, lifecycle ownership: ' + execute(exe, runners),
              flush=True)
        if args.mutate:
            original = (ROOT / PRODUCT).read_text()
            mutated = work / 'pc98_init.c'
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, original.count(old))
                mutated.write_text(original.replace(old, new))
                exe = compile_host(work, mutated)
                print(f'MUTATE RED {name} (compiled; runtime): '
                      + execute(exe, runners, red=True), flush=True)
    return 0


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_pc98init.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    raise SystemExit(main())
