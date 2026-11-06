#!/usr/bin/env python3
"""Fixed mode trace oracle, independent of product output; libc-free qemu.

MIT source notices: drivers/video/trident/trident.h:1-48, retained also in
mode_640.c. Oracle transcribed from trident_driver-1.4.0.c:1004-1044,
trident_dac.c:256-278,631-749,781-810,871-907 and
vgaHW.c:369-488,574-604,845-872,1219-1353,1391-1468.
Recreate: specialize those functions for PROVIDIA9682, 640/656/752/800,
480/490/492/525, packed8, PCI MMIO, NoAccel, PCIBurst on, PCIRetry off;
use the two old-clock search results from trident_pll.c:42-135.
No PM-local reference files are needed at runtime or in CI.
"""
CHECK = dict(name='trident-mode', tier=1, needs=[], budget=30,
    covers=['drivers/video/trident/mode_640.c',
            'drivers/video/trident/trident.h', 'tools/tests/trident_mode_host.c',
            'tools/tests/trident_device_fake.c', 'tools/tests/trident_device_fake.h',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'build/config.mk', 'include/types.h', 'include/memmap.h',
            'gfx/palette.h', 'include/pegc.h'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'drivers/video/trident/mode_640.c'
HOST = 'tools/tests/trident_mode_host.c'
FAKE = 'tools/tests/trident_device_fake.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
MUTATIONS = (
    ('pll-ntsc', '{ 0xE5, 0x1E }', '{ 0xE4, 0x1E }'),
    ('pll-pal', '{ 0xA4, 0x0E }', '{ 0xA4, 0x1E }'),
    ('reference-select', '(d->reference & 0x80) != 0', '(d->reference & 0x40) != 0'),
    ('reference-stale', 'base, 0xC0, &ignored', 'base, 0xC1, &ignored'),
    ('reference-cache', 'if (d->reference == 0)', 'if (1)'),
    ('current-base', 'u16 base = d->cur_vga_base;', 'u16 base = TRI_CR_COLOR;'),
    ('vga-width', 'return trident_write8(d, TRI_SPACE_REGS, port + 1, value);',
     'return trident_indexed_write(d, port, index, value);'),
    ('vga-last-cr', '0x0B, 0xC3, 0xFF', '0x0B, 0xC3, 0xFE'),
    ('vga-gr6', '0x40, 0x05, 0x0F', '0x40, 0x0C, 0x0F'),
    ('vga-sr1', '{ 0x01, 0x0F, 0x00, 0x0E }', '{ 0x21, 0x0F, 0x00, 0x0E }'),
    ('vga-offset', '0xEA, 0x2C, 0xDF, 0x28', '0xEA, 0x2C, 0xDF, 0x50'),
    ('ar-last', '0x41, 0, 0x0F, 0, 0', '0x41, 0, 0x0F, 0, 1'),
    ('ar-enable', 'palette_access(d, true)', 'palette_access(d, false)'),
    ('hidden-dac', 'trident_hidden_dac_write(d, 0)', 'trident_hidden_dac_write(d, 1)'),
    ('linear', '0x21, 0x20)', '0x21, 0x00)'),
    ('graph-engine', '0x36, 0x80)', '0x36, 0)'),
    ('dram-control-select', 'base, 0x3A));', 'base, 0x36));'),
    ('dram-control', '0x3A, 0)', '0x3A, 0x10)'),
    ('addcol-mask', 'addcol & 0xCF', 'addcol & 0xEF'),
    ('performance', 'performance | 0x10', 'performance & 0xEF'),
    ('interface', 'interface | 0x40', 'interface & 0xBF'),
    ('pci-burst', '(pci & 0xF9) | 0x06', '(pci & 0xF9)'),
    ('retry', '0x55, 0x1F)', '0x55, 0xDF)'),
    ('misc-int', 'misc_int | 0x04', 'misc_int & 0xFB'),
    ('misc-ext', '(misc_ext & 0xF0) | 0x02', '(misc_ext & 0xF0) | 0x04'),
    ('pitch', '#define MODE_OFFSET (TRI_PITCH >> 3)', '#define MODE_OFFSET (TRI_PITCH >> 4)'),
    ('clock-misc', '#define MODE_CLOCK_MISC 0xEBu', '#define MODE_CLOCK_MISC 0xE3u'),
    ('sr0e-xor', '0xC0 ^ 0x02', '0xC0'),
    ('protect-entry', 'trident_protect(d, base, true)', 'trident_protect(d, base, false)'),
    ('protect-exit', 'trident_protect(d, TRI_CR_COLOR, false)',
     'trident_protect(d, TRI_CR_COLOR, true)'),
    ('drop-rc', 'if (rc) return rc;', 'if (rc) return TRI_E_ACCESS;'),
)


def oracle(base, reference, seed, misc=None):
    """Explicit reference operation order; no product parser or device model."""
    out = []
    def w(p, v, width=1):
        out.append((p, v, width, 1))
    def r(p, v):
        out.append((p, v, 1, 0))
    def rd(p, i, v):
        w(p, i)
        r(p + 1, v)
    def pair(p, i, v):
        w(p, i)
        w(p + 1, v)
    def ext(p, i, v):
        w(p, i | v << 8, 2)
    inactive = misc is not None and bool(misc & 1) != (base == 0x3D4)
    cr = lambda i: 255 if inactive else (seed + i * (7 if base == 0x3D4 else 11)) & 255
    sr = lambda i: (seed + i * 17) & 255
    gr = lambda i: (seed + i * 13) & 255
    # ModeInit unlock, then TridentInit's reads (including unused video reads).
    rd(base, 0x11, cr(0x11))
    pair(base, 0x11, cr(0x11) & 127)
    rd(0x3C4, 0x0B, sr(0x0B))
    rd(0x3CE, 0x0F, gr(0x0F))
    rd(base, 0xC0, reference)
    rd(base, 0x2A, cr(0x2A))
    rd(base, 0x2F, cr(0x2F))
    w(base, 0x3A)
    rd(base, 0x29, cr(0x29))
    rd(0x3CE, 0x2F, gr(0x2F))
    rd(base, 0x39, cr(0x39))
    rd(0x3C4, 0x20, sr(0x20))
    rd(0x3C4, 0x12, sr(0x12))
    # Protect, followed by vgaHWRestoreMode. Sequencer image is unmodified 01.
    rd(0x3C4, 1, sr(1))
    pair(0x3C4, 0, 1)
    pair(0x3C4, 1, sr(1) | 32)
    r(base + 6, 255 if inactive else 0)
    w(0x3C0, 0)
    w(0x3C2, 0xE3)
    for i, v in enumerate((1, 15, 0, 14), 1):
        pair(0x3C4, i, v)
    pair(0x3D4, 17, 0x2C)
    # Full vgaHWInit table; standard pitch guess precedes Trident Offset.
    crt = (95, 79, 79, 128, 82, 30, 11, 62, 0, 64, 0, 0, 0, 0, 0, 0,
           234, 44, 223, 40, 0, 223, 11, 195, 255)
    for i, v in enumerate(crt):
        pair(0x3D4, i, v)
    for i, v in enumerate((0, 0, 0, 0, 0, 64, 5, 15, 255)):
        pair(0x3CE, i, v)
    r(0x3DA, 0)
    w(0x3C0, 0)
    for i, v in enumerate((*range(16), 65, 0, 15, 0, 0)):
        r(0x3DA, 0)
        w(0x3C0, i)
        w(0x3C0, v)
    r(0x3DA, 0)
    w(0x3C0, 32)
    # TridentRestore; word accesses must remain single trace events.
    rd(0x3C4, 0x0B, sr(0x0B))
    ext(0x3C4, 14, 194)
    r(0x3C8, 0x37)
    for _ in range(4):
        r(0x3C6, 0xA5)
    w(0x3C6, 0)
    r(0x3C8, 0x37)
    for p, i, v in (
        (0x3D4, 0x1E, 128), (0x3D4, 0x21, 32), (0x3C4, 13, 32),
        (0x3D4, 0x50, 0), (0x3D4, 0x27, 8), (0x3D4, 0x2B, 0),
        (0x3D4, 0x29, cr(0x29) & 207), (0x3D4, 0x36, 128),
        (0x3D4, 0x2F, cr(0x2F) | 16), (0x3D4, 0x2A, cr(0x2A) | 64),
        (0x3D4, 0x3A, 0), (0x3D4, 0x38, 0),
        (0x3D4, 0x39, (cr(0x39) & 249) | 6), (0x3D4, 0x55, 31),
        (0x3CE, 0x2F, gr(0x2F) | 4),
        (0x3CE, 15, (gr(15) & 240) | 2), (0x3D4, 19, 80)):
        ext(p, i, v)
    a, b = (164, 14) if reference & 128 else (229, 30)
    w(0x43C8, a)
    w(0x43C9, b)
    w(0x3C2, 235)
    ext(0x3C4, 14, 194)
    rd(0x3C4, 1, 1)
    pair(0x3C4, 1, 1)
    pair(0x3C4, 0, 3)
    r(0x3DA, 0)
    w(0x3C0, 32)
    return out


def fixture():
    text = ('struct mode_case { u16 base; u8 reference, seed, misc; '
            'const struct trident_fake_event *events; u32 count; };\n')
    rows = []
    configs = [(b, r, s, int(b == 0x3D4))
               for b in (0x3B4, 0x3D4) for r in (0, 0x40, 0x80, 0xFF)
               for s in (0, 0x5A, 0xFF)]
    # First ModeInit after Save(mono) -> PC98 Init(MISC=color):
    # the reference reads the inactive mono ports, hence FF and PAL.
    configs.append((0x3B4, 0xFF, 0x5A, 1))
    for base, reference, seed, misc in configs:
        name = f'expected_{len(rows)}'
        events = oracle(base, reference, seed, misc)
        text += f'static const struct trident_fake_event {name}[] = {{\n'
        for p, v, width, write in events:
            text += f'{{.space=0,.offset={p},.value={v},.count=1,.width={width},.write={write},.rc=0}},\n'
        text += '};\n'
        rows.append(f'{{{base},{reference},{seed},{misc},{name},sizeof({name})/sizeof({name}[0])}}')
    return text + 'static const struct mode_case cases[] = {\n' + ',\n'.join(rows) + '\n};\n'


def build(work, product):
    exe = work / 'mode'
    subprocess.run(['gcc', *FLAGS, '-I' + str(work), '-I' + str(ROOT),
                    '-I' + str(ROOT / 'drivers/video/trident'), *include_flags(ROOT),
                    '-nostdlib', '-static', '-no-pie', str(ROOT / HOST),
                    str(ROOT / FAKE), str(product), '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    assert runners, 'no runners'
    with tempfile.TemporaryDirectory(prefix='trident-mode-') as tmp:
        work = Path(tmp)
        (work / 'mode_expected.h').write_text(fixture())
        product = ROOT / PRODUCT
        original = product.read_text()
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), *FLAGS, '-I' + str(ROOT), *include_flags(ROOT),
                        '-c', str(product), '-o', str(work / 'mode.o')], check=True)
        exe = build(work, product)
        for runner in runners:
            result = host32.run([str(exe)], runner=runner, capture_output=True)
            assert result.returncode == 0 and result.stdout == b'GREEN\n', result
            print(f'PASS 25 complete mode traces, mismatched decode, cached reentry, every I/O failure ({runner})', flush=True)
        if args.mutate:
            copied = work / 'mode_640.c'
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, original.count(old))
                copied.write_text(original.replace(old, new))
                exe = build(work, copied)  # compile failure is never runtime RED
                for runner in runners:
                    result = host32.run([str(exe)], runner=runner, capture_output=True)
                    assert result.returncode == 1 and result.stdout.startswith(b'RED '), (name, result)
                    print(f'MUTATE RED {name} ({runner})', flush=True)
    return 0


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', str(ctx.root / 'tools/tests/test_trident_mode.py'),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    raise SystemExit(main())
