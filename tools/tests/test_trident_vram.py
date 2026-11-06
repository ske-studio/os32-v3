#!/usr/bin/env python3
"""SPR full table and nondestructive continuous VRAM probe, libc-free.

SPR oracle: MIT trident_driver-1.4.0.c:2919-2975, transcribed as KiB in
trident_vram_host.c. Probe acceptance: TRIDENT_S3_DESIGN §§4,7, extending
trident_mode.hwb:411-431. Init oracle macros are reused from the existing
independent MIT fixture, never from product code/output or PM-local sources.
"""
CHECK = dict(name='trident-vram', tier=1, needs=[], budget=60,
    covers=['drivers/video/trident/vram.c', 'drivers/video/trident/trident.h',
            'drivers/video/trident/regs.c', 'drivers/video/trident/pc98_init.c',
            'drivers/video/trident/mode_640.c', 'drivers/video/trident/route_ra266.c',
            'tools/tests/trident_vram_host.c', 'tools/tests/trident_device_fake.c',
            'tools/tests/trident_device_fake.h', 'tools/tests/trident_pc98init_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'build/config.mk', 'include/types.h', 'include/memmap.h',
            'include/io.h', 'gfx/palette.h', 'include/pegc.h', 'lib/serial_diag.h'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'drivers/video/trident/vram.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
MUTATIONS = (
    ('M_PLAN_AFTER_INIT', 'if (!d->armed || !d->pinned) return TRI_E_STATE;',
     'if (!d->init_done || !d->armed || !d->pinned) return TRI_E_STATE;'),
    ('M_SPR_CAP', 'case 0x0F: kib = 4096;', 'case 0x0F: kib = 2048;'),
    ('M_SPR_DEFAULT', 'default: kib = 1024; unknown = true;',
     'default: kib = 2048; unknown = true;'),
    ('M_SPR_MASK', 'switch (spr & 0x0F)', 'switch (spr & 0x07)'),
    ('M_SPR_EXT', 'case 0x10: kib = 28672;', 'case 0x10: kib = 24576;'),
    ('M_SPR_BASE', 'u16 base = d->cur_vga_base;', 'u16 base = TRI_CR_COLOR;'),
    ('M_PROBE_INVERT', 'phase < 2 && !bad', 'phase < 1 && !bad'),
    ('M_PROBE_UNDO', 'WRITE(at, saved[i]);', 'WRITE(at, saved[i] ^ 1u);'),
    ('M_PROBE_TAGS', 'value != pattern(at)', '(value != pattern(at) && off < WORD_BYTES)'),
    ('M_LOWER_RECHECK', '!rc && end < limit && end', '!rc && end < limit && !end'),
    ('M_DECODE_BOUND', 'u32 limit = d->res->decode_bytes;',
     'u32 limit = TRI_BAR0_DECODE_BYTES;'),
    ('M_REQUIRE_2M', 'd->verified_bytes >= TRI_DISPLAY_BYTES ? TRI_OK : rc',
     'd->verified_bytes >= TRI_BAR0_DECODE_BYTES / 2 ? TRI_OK : (rc ? rc : TRI_E_CAP_TOO_SMALL)'),
    ('M_DECLARE_LIMIT', 'u32 limit = d->res->decode_bytes;',
     'u32 limit = d->declared_bytes;'),
    ('M_RESTORE_RC', 'return restored;', 'return 0;'),
    ('M_CLEANUP_FATAL', 'return d->verified_bytes >= TRI_DISPLAY_BYTES ? TRI_OK : rc;',
     'return rc;'),
    ('M_DISPLAY_RESTORE', 'if (*complete > restore_end) *complete = restore_end;',
     'if (*complete > restore_end) *complete = *complete;'),
    ('M_UPPER_PHASES', 'words && !bad && !stop', 'words && !stop'),
    ('M_LOG', 'diagnostic(d, rc, access_rc);',
     'if (rc) diagnostic(d, rc, access_rc);'),
)


def init_fixture():
    text = (ROOT / 'tools/tests/trident_pc98init_host.c').read_text()
    macros = text[text.index('#define EVENT('):text.index('#define LENGTH(')]
    # Reset values, color base, alternate SR0C branch. Status 5A is seeded by
    # the harness solely to match the existing fixture's named status sample.
    values = dict(misc=1, seq=0, old=0, ext=0, config=0, video=0,
                  base=0x3D4, mono=0, cr11=0)
    return (macros + '\n' + ''.join(f'#define {k} {v}\n' for k, v in values.items())
            + 'static const struct trident_fake_event init_expected[] = {\n'
              'PREFIX, CONFIG_ALTERNATE, SUFFIX\n};\n'
            + ''.join(f'#undef {k}\n' for k in values))


def build(work, source):
    exe = work / 'vram'
    sources = ['tools/tests/trident_vram_host.c', 'tools/tests/trident_device_fake.c',
               'drivers/video/trident/regs.c', 'drivers/video/trident/pc98_init.c',
               'drivers/video/trident/mode_640.c', 'drivers/video/trident/route_ra266.c']
    subprocess.run(['gcc', *FLAGS,
                    '-I' + str(work), '-I' + str(ROOT), *include_flags(ROOT),
                    '-I' + str(ROOT / 'drivers/video/trident'),
                    '-nostdlib', '-static', '-no-pie', str(source),
                    *(str(ROOT / s) for s in sources), '-o', str(exe)], check=True)
    return exe


def execute(exe, runners, red=False):
    for runner in runners:
        result = host32.run([str(exe)], runner=runner, capture_output=True)
        assert result.returncode == (1 if red else 0), result
        assert (result.stdout.startswith(b'RED vram line ') if red else
                result.stdout == b'GREEN vram\n'), result
    return result.stdout.decode().strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    assert runners
    with tempfile.TemporaryDirectory(prefix='trident-vram-') as tmp:
        work = Path(tmp)
        (work / 'vram_init_expected.h').write_text(init_fixture())
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), *FLAGS, '-I' + str(ROOT), *include_flags(ROOT),
                        '-c', str(ROOT / PRODUCT), '-o', str(work / 'vram.o')], check=True)
        print('PASS production TU cross compile', flush=True)
        original = (ROOT / PRODUCT).read_text()
        print('PASS SPR/VRAM/restore/production binding: '
              + execute(build(work, ROOT / PRODUCT), runners), flush=True)
        if args.mutate:
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, original.count(old))
                mutated = work / 'vram.c'
                mutated.write_text(original.replace(old, new))
                print(f'MUTATE RED {name} (compiled; runtime): '
                      + execute(build(work, mutated), runners, red=True), flush=True)
    return 0


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_vram.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    raise SystemExit(main())
