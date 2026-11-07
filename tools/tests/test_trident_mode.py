#!/usr/bin/env python3
"""Independent mode trace oracle for all 15 candidates, PLL references and MUX.
MIT references: trident_driver-1.4.0.c:952-1044,1691-1711,
trident_dac.c:253-271,564-908, trident_pll.c:42-135 and
vgaHW.c:1185-1353,1391-1468. No PM-local files needed at runtime.
"""
CHECK = dict(name='trident-mode', tier=1, needs=[], budget=300,
    covers=['drivers/video/trident/mode_640.c', 'drivers/video/trident/vram.c',
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
import struct

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'drivers/video/trident/mode_640.c'
HOST = 'tools/tests/trident_mode_host.c'
FAKE = 'tools/tests/trident_device_fake.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
MUTATIONS = (
    ('pll-ntsc', '#define MODE_NTSC_SIG 15013700u', '#define MODE_NTSC_SIG 16013700u'),
    ('pll-pal', '#define MODE_PAL_SIG 9297975u', '#define MODE_PAL_SIG 10297975u'),
    ('vga-last-cr', 'cr[24] = 0xFF;', 'cr[24] = 0xFE;'),
    ('vga-offset', 'cr[19] = mode->display_width >> 4;',
     'cr[19] = mode->display_width >> 3;'),
    ('hidden-dac', 'u8 dac = 0, pixel_bus = 0', 'u8 dac = 1, pixel_bus = 0'),
    ('misc-ext', 'newmode2 = 0x20, ext = 0x02', 'newmode2 = 0x20, ext = 0x04'),
    ('packed24-image', 'pixel_bus = 0x29;', 'pixel_bus = 0x09;'),
    ('reference-stale', 'base, 0xC0, &ignored', 'base, 0xC1, &ignored'),
    ('vga-gr6', '0x40, 0x05, 0x0F', '0x40, 0x0C, 0x0F'),
    ('vga-sr1', '{ 0x01, 0x0F, 0x00, 0x0E }', '{ 0x21, 0x0F, 0x00, 0x0E }'),
    ('ar-last', '0x41, 0, 0x0F, 0, 0', '0x41, 0, 0x0F, 0, 1'),
    ('dram-control-select', 'base, 0x3A));', 'base, 0x36));'),
    ('dram-control', '0x3A, 0)', '0x3A, 0x10)'),
    ('addcol-mask', 'addcol & 0xCF', 'addcol & 0xEF'),
    ('protect-entry', 'trident_protect(d, base, true)', 'trident_protect(d, base, false)'),
    ('pll-tie', 'if (error < diff)', 'if (error <= diff)'),
    ('pll-reference', '(d->reference & 0x80) != 0', '(d->reference & 0x40) != 0'),
    ('reference-cache', 'if (d->reference == 0)', 'if (1)'),
    ('current-base', 'u16 base = d->cur_vga_base;', 'u16 base = TRI_CR_COLOR;'),
    ('vga-width', 'return trident_write8(d, TRI_SPACE_REGS, port + 1, value);',
     'return trident_indexed_write(d, port, index, value);'),
    ('ar-enable', 'palette_access(d, true)', 'palette_access(d, false)'),
    ('linear', '0x21, 0x20)', '0x21, 0x00)'),
    ('graph-engine', 'no_accel ? 0 : 0x80', '0'),
    ('offset-high', '((offset & 0x300) >> 4)', '0'),
    ('performance', 'performance | 0x10', 'performance & 0xEF'),
    ('interface', 'interface | 0x40', 'interface & 0xBF'),
    ('pci-burst', 'd->settings.ram_fallback ? 0 : 0x06',
     'd->settings.ram_fallback ? 0 : 0'),
    ('pci-no-burst-fallback', 'd->settings.ram_fallback ? 0 : 0x06',
     'd->settings.ram_fallback ? 0x06 : 0x06'),
    ('retry', '0x55, 0x1F)', '0x55, 0xDF)'),
    ('misc-int', 'misc_int | 0x04', 'misc_int & 0xFB'),
    ('pitch', 'm.display_width = width;', 'm.display_width = m.hd;'),
    ('noaccel-pitch', 'm.display_width = m.hd;', 'm.display_width = 1024;'),
    ('format-24-32', '640,656,752,800,v,vs,ve,vt,0,24,24,',
     '640,656,752,800,v,vs,ve,vt,0,24,32,'),
    ('mux-boundary', 'return m->dot_khz > threshold;', 'return m->dot_khz >= threshold;'),
    ('mux-horizontal', 'hd >>= 1; hs >>= 1; he >>= 1; ht >>= 1;', 'hd >>= 0; hs >>= 0; he >>= 0; ht >>= 0;'),
    ('dac-565', '0x10 : 0x30', '0x10 : 0x10'),
    ('pll-32', 'mode_clock(m.dot_khz * (m.bpp == 32 ? 2u : 1u),',
     'mode_clock(m.dot_khz * (m.bpp == 32 ? 1u : 1u),'),
    ('clock-misc', '#define MODE_CLOCK_SELECT 0x08u', '#define MODE_CLOCK_SELECT 0x00u'),
    ('sr0e-xor', '0xC0 ^ 0x02', '0xC0'),
    ('protect-exit', 'trident_protect(d, TRI_CR_COLOR, false)',
     'trident_protect(d, TRI_CR_COLOR, true)'),
    ('drop-rc', 'if (rc) return rc;', 'if (rc) return TRI_E_ACCESS;'),
)


def pll(clock, pal):
    # P:93 evaluates single precision after each operation, strict tie order.
    f32 = lambda x: struct.unpack('f', struct.pack('f', x))[0]
    ref = f32(17.73448 if pal else 14.31818)
    best, result = 750, None
    for k in range(int(clock > 50000), 2):
        for n in range(122):
            for m in range(1, 32):
                freq = int(f32(f32(f32((n + 8) * ref) / ((m + 2) * (1 << k))) * 1000))
                diff = abs(freq - clock)
                if diff < best:
                    best, result = diff, (((m & 1) << 7) | n, (m >> 1) | (k << 4))
    assert result is not None
    return result


PLL = {(bpp, pal): pll(25175 * (2 if bpp == 32 else 1), pal)
       for bpp in (8, 16, 24, 32) for pal in (False, True)}



def oracle(base, reference, seed, misc=None, mode=0, noaccel=True, mux=False,
           fallback=False):
    """Explicit reference operation order; no product parser or device model."""
    timing, fmt = divmod(mode, 5)
    depth, bpp = ((8, 8), (15, 16), (16, 16), (24, 24), (24, 32))[fmt]
    height, vs, ve, vt, polarity = ((480,490,492,525,0xC0),
                                  (400,412,414,449,0x40),
                                  (350,387,389,449,0x80))[timing]
    noaccel = noaccel or fallback or bpp == 24
    width = 640 if noaccel else 1024
    offset = width * (bpp // 8) // 8
    multiplex = mux and bpp == 8
    dac = (0, 0x10, 0x30, 0xD0, 0xD0)[fmt] | (0x20 if multiplex else 0)
    bus = (0, 5, 5, 0x29, 9)[fmt] | int(multiplex)
    misc_mode = 0x23 | polarity
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
    w(0x3C2, misc_mode)
    for i, v in enumerate((1, 15, 0, 14), 1):
        pair(0x3C4, i, v)
    pair(0x3D4, 17, (ve & 15) | 32)
    # Full vgaHWInit table; standard pitch guess precedes Trident Offset.
    # VGA timing image, independent arithmetic from the specified timings.
    hd, hs, he, ht = (320,328,376,400) if multiplex else (640,656,752,800)
    hb = (ht // 8 - 1) & 63
    hb = 0 if hb > ((hd // 8 - 1) & 63) else hb - 1
    overflow = (((vt-2)>>8)&1) | (((height-1)>>7)&2) | ((vs>>6)&4) | \
               (((height-1)>>5)&8) | 16 | (((vt-2)>>4)&32) | \
               (((height-1)>>3)&64) | ((vs>>2)&128)
    vb = vt - 1
    vb = 0 if ((vb & 255) > ((height-1)&255) and
                  (vb & 127) > ((height-1)&127)) else (vb & 255) - 1
    crt = [ht//8-5, hd//8-1, hd//8-1, (hb&31)|128, hs//8,
           ((hb&32)<<2)|((he//8)&31), (vt-2)&255, overflow, 0, 64,
           0,0,0,0,0,0,vs&255,(ve&15)|32,(height-1)&255,width//16,
           0,(height-1)&255,vb&255,195,255]
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
    w(0x3C6, dac)
    r(0x3C8, 0x37)
    for p, i, v in (
        (0x3D4, 0x1E, 128), (0x3D4, 0x21, 32), (0x3C4, 13, 32 | (2 if multiplex else 0)),
        (0x3D4, 0x50, 0), (0x3D4, 0x27, 8), (0x3D4, 0x2B, 0),
        (0x3D4, 0x29, (cr(0x29) & 207) | ((offset & 0x300) >> 4)), (0x3D4, 0x36, 0 if noaccel else 128),
        (0x3D4, 0x2F, cr(0x2F) | 16), (0x3D4, 0x2A, cr(0x2A) | 64),
        (0x3D4, 0x3A, 0), (0x3D4, 0x38, bus),
        (0x3D4, 0x39, (cr(0x39) & 0xF9) | (0 if fallback else 0x06)), (0x3D4, 0x55, 31),
        (0x3CE, 0x2F, gr(0x2F) | 4),
        (0x3CE, 15, (gr(15) & 240) | (10 if bpp == 32 else 2)), (0x3D4, 19, offset & 255)):
        ext(p, i, v)
    a, b = PLL[bpp, bool(reference & 128)]
    w(0x43C8, a)
    w(0x43C9, b)
    w(0x3C2, (misc_mode & 0xF3) | 8)
    ext(0x3C4, 14, 194)
    rd(0x3C4, 1, 1)
    pair(0x3C4, 1, 1)
    pair(0x3C4, 0, 3)
    r(0x3DA, 0)
    w(0x3C0, 32)
    return out


def fixture(fallback):
    text = ('struct mode_case { u16 base; u8 reference, seed, misc; '
            'u8 mode, noaccel, mux, pll_lo, pll_hi; u16 pitch; '
            'const struct trident_fake_event *events; u32 count; };\n')
    rows = []
    configs = [(b, r, s, int(b == 0x3D4))
               for b in (0x3B4, 0x3D4) for r in (0, 0x40, 0x80, 0xFF)
               for s in (0, 0x5A, 0xFF)]
    # First ModeInit after Save(mono) -> PC98 Init(MISC=color):
    # the reference reads the inactive mono ports, hence FF and PAL.
    configs.append((0x3B4, 0xFF, 0x5A, 1))
    configs = [(*c, 0, True, False) for c in configs]
    configs += [(0x3D4, ref, 0x5A, 1, mode, na, mux)
                for mode in range(15) for na in (False, True)
                for ref in (0, 128) for mux in (False, True)]
    for base, reference, seed, misc, mode, na, mux in configs:
        name = f'expected_{len(rows)}'
        events = oracle(base, reference, seed, misc, mode, na, mux, fallback)
        text += f'static const struct trident_fake_event {name}[] = {{\n'
        for p, v, width, write in events:
            text += f'{{.space=0,.offset={p},.value={v},.count=1,.width={width},.write={write},.rc=0}},\n'
        text += '};\n'
        bpp = (8, 16, 16, 24, 32)[mode % 5]
        lo, hi = PLL[bpp, bool(reference & 128)]
        pitch = (640 if na or fallback or bpp == 24 else 1024) * (bpp // 8)
        rows.append(f'{{{base},{reference},{seed},{misc},{mode},{int(na)},{int(mux)},{lo},{hi},{pitch},{name},sizeof({name})/sizeof({name}[0])}}')
    return text + 'static const struct mode_case cases[] = {\n' + ',\n'.join(rows) + '\n};\n'


def build(work, product, fallback):
    (work / 'mode_expected.h').write_text(fixture(fallback))
    exe = work / 'mode'
    subprocess.run(['gcc', *FLAGS, f'-DTRI_RAM_FALLBACK_DEFAULT={fallback}',
                    '-I' + str(work), '-I' + str(ROOT),
                    '-I' + str(ROOT / 'drivers/video/trident'), *include_flags(ROOT),
                    '-nostdlib', '-static', '-no-pie', str(ROOT / HOST),
                    str(ROOT / FAKE), str(ROOT / 'drivers/video/trident/vram.c'), str(product), '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    assert runners, 'no runners'
    with tempfile.TemporaryDirectory(prefix='trident-mode-') as tmp:
        work = Path(tmp)
        product = ROOT / PRODUCT
        original = product.read_text()
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), *FLAGS, '-I' + str(ROOT), *include_flags(ROOT),
                        '-c', str(product), '-o', str(work / 'mode.o')], check=True)
        for fallback in (0, 1):
            exe = build(work, product, fallback)
            for runner in runners:
                result = host32.run([str(exe)], runner=runner, capture_output=True)
                assert result.returncode == 0 and result.stdout == b'GREEN\n', result
                print(f'PASS 145 complete mode traces, mismatched decode, cached reentry, every I/O failure (fallback={fallback}, {runner})', flush=True)
        if args.mutate:
            copied = work / 'mode_640.c'
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, original.count(old))
                copied.write_text(original.replace(old, new))
                fallback = int(name == 'pci-no-burst-fallback')
                exe = build(work, copied, fallback)  # compile failure is never runtime RED
                for runner in runners:
                    result = host32.run([str(exe)], runner=runner, capture_output=True)
                    assert result.returncode == 1 and result.stdout.startswith(b'RED '), (name, result)
                    print(f'MUTATE RED {name} (fallback={fallback}, {runner})', flush=True)
            for fallback in (0, 1):
                exe = build(work, product, fallback)
                for runner in runners:
                    result = host32.run([str(exe)], runner=runner, capture_output=True)
                    assert result.returncode == 0 and result.stdout == b'GREEN\n', result
                    print(f'RESTORED GREEN (fallback={fallback}, {runner})', flush=True)
    return 0


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', str(ctx.root / 'tools/tests/test_trident_mode.py'),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    raise SystemExit(main())
