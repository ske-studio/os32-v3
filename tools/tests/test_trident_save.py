#!/usr/bin/env python3
"""Trident 3b Save: libc-free shared fake, independent script trace/slot oracle.

The C fixture records H:587-1496 / vgaHW.c:1016-1166 / trident_dac.c:912-1048.
No test reads references outside the repository. regs.c/default_dac.c are
parallel work: this unit links register adapters and the H default-DAC fixture.
Production assembly/ScreenInit ordering is deferred to 3c integration.
"""
CHECK = dict(name='trident-save', tier=1, needs=[], budget=30,
             covers=['drivers/video/trident/state_save.c',
                     'drivers/video/trident/trident.h',
                     'tools/tests/trident_save_host.c',
                     'tools/tests/trident_device_fake.c',
                     'tools/tests/trident_device_fake.h',
                     'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
                     'userland/tests/hw/trident_mode.hwb', 'build/config.mk',
                     'drivers/char/serial.h', 'lib/serial_diag.h',
                     'include/types.h', 'include/memmap.h', 'gfx/palette.h'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'drivers/video/trident/state_save.c'
HOST = 'tools/tests/trident_save_host.c'
FAKE = 'tools/tests/trident_device_fake.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
         '-fno-builtin']
# Unique anchors; compile errors/signals are never accepted as runtime RED.
# name, product, old, new, runtime assertion/oracle that rejects the mutant.
MUTATIONS = (
    ('M_SAVE_GR6', PRODUCT,
     'i < 9; ++i)', 'i < 6; ++i)', 'VGA GR trace/slots including GR06=0C'),
    ('M_DAC_LAST', PRODUCT,
     'for (u32 i = 6; i < TRI_DAC_BYTES; ++i)',
     'for (u32 i = 6; i < TRI_DAC_BYTES - 1; ++i)', 'DAC last component/trace'),
    ('M_DEFAULT_DAC_COPY', PRODUCT,
     'save[i] = trident_default_dac[i];',
     'save[i] = 0;', 'unreadable DAC default slots'),
    ('M_SAVE_CR3A', PRODUCT,
     '0x3A, 0x29, 0x38', '0x3B, 0x29, 0x38', 'extended CR3A trace/slot'),
    ('M_SAVE_MONO', PRODUCT,
     'd->saved.vga_base = base;', 'd->saved.vga_base = TRI_CR_COLOR;', 'mono trace/base'),
    ('M_SAVE_MCLK', PRODUCT,
     'if (TRI_SAVE_MCLK)', 'if (!TRI_SAVE_MCLK)', 'no MCLK reads/slots'),
    ('M_SAVE_SR0E', PRODUCT,
     'return trident_indexed_write(d, TRI_SR_INDEX, 0x0E, save[TRI_SAVE_SR0E]);',
     'return trident_indexed_write(d, TRI_SR_INDEX, 0x0E, 0);', 'final SR0E trace/value'),
    ('M_SAVE_VALID', PRODUCT,
     'd->saved.valid = true;', 'd->saved.valid = false;', 'full save commits valid'),
    ('M_SAVE_DIAGNOSTIC', PRODUCT,
     'serial_puts("[trident] stage=save-palette rc=-6 default DAC\\n");',
     'if (!badmap) serial_puts("[trident] stage=save-palette rc=-6 default DAC\\n");',
     'unreadable DAC diagnostic count'),
    ('M_SAVE_AGAIN', PRODUCT,
     'if (d->saved.valid) return 0;', 'if (d->saved.valid) { /* mutated */ }',
     'second save zero accesses/immutable image'),
    ('M_SAVE_DROP_RC', PRODUCT,
     'if (rc) return rc;', 'if (rc) return TRI_E_ACCESS;', 'each access preserves -73'),
)


def build(work, source):
    exe = work / 'save'
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT),
                    '-I' + str(ROOT / 'drivers/video/trident'), *include_flags(ROOT),
                    '-nostdlib', '-static', '-no-pie', str(source),
                    str(ROOT / HOST), str(ROOT / FAKE), '-o', str(exe)],
                   cwd=ROOT, check=True)
    return exe


def execute(exe, runner, red=False):
    result = host32.run([str(exe)], runner=runner, capture_output=True)
    if red:
        assert result.returncode == 1 and result.stdout.startswith(b'RED save line '), result
    else:
        assert result.returncode == 0 and result.stdout == b'GREEN\n', result
    return result.stdout.decode().strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    assert runners, 'no host32 runners'
    with tempfile.TemporaryDirectory(prefix='trident-save-') as tmp:
        work = Path(tmp)
        # Cross-compile the real TU too; make all cannot link parallel 3b yet.
        compiler = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        subprocess.run([str(compiler), *FLAGS, '-I' + str(ROOT), *include_flags(ROOT),
                        '-c', str(ROOT / PRODUCT), '-o', str(work / 'save.o')], check=True)
        exe = build(work, ROOT / PRODUCT)
        for runner in runners:
            print(f'PASS save trace, all slots, mono/color, DAC fallback, immutable save, every I/O failure ({runner}): '
                  + execute(exe, runner), flush=True)
        if args.mutate:
            original = (ROOT / PRODUCT).read_text()
            for name, path, old, new, rejects in MUTATIONS:
                assert path == PRODUCT and original.count(old) == 1, (name, original.count(old))
                source = work / 'state_save.c'
                source.write_text(original.replace(old, new))
                exe = build(work, source)
                for runner in runners:
                    print(f'MUTATE RED {name} ({runner}, {rejects}): '
                          + execute(exe, runner, red=True), flush=True)
    return 0


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_save.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    raise SystemExit(main())
