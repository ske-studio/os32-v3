"""OSRI fixed conversion and loader tests with generated images and VFS stub."""
CHECK = dict(name='osri-host', tier=1,
             covers=['gfx/gfx_osri.c', 'gfx/gfx_osri.h', 'tools/img2osri.py',
                     'lib/le_internal.h', 'lib/crc32_core.inc', 'tools/tests/host32.py'], needs=[], budget=60)

import argparse
import ctypes
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
WRAPPER = r'''
#include "gfx_osri.h"
#include "gfx_hal.h"
#include "../lib/crc32_core.inc"
extern unsigned char data[]; extern unsigned char expected_planes[];
static unsigned int pos, chunk, length; static int closed;
int vfs_open(const char *p, int mode) { (void)p; (void)mode; pos=0; closed=0; return 3; }
void vfs_close(int fd) { (void)fd; closed++; }
int vfs_read_fd(int fd, void *p, unsigned int n) {
 (void)fd; if (n > chunk) n=chunk; if (n > length-pos) n=length-pos;
 for (unsigned int i=0;i<n;i++) ((unsigned char *)p)[i]=data[pos+i];
 pos+=n; return n;
}
static int run(int badfb) {
 static unsigned char planes[128000]; struct OsriImage im;
 struct gfx_kernel_fb fb = {640,400,80,GFX_BB_PLANAR4,{0}};
 for (int i=0;i<4;i++) fb.planes[i]=planes+i*32000;
 if (badfb) fb.planes[2]++;
 int rc=osri_load("x", &fb, &im);
 if (!badfb && closed != 1) return 99;
 if (!badfb && rc == 0 && length == 128096)
  for (int i=0;i<128000;i++) if (planes[i] != expected_planes[i]) return 98;
 return rc;
}
static void put32(int off, unsigned int v) { for (int i=0;i<4;i++) data[off+i]=(unsigned char)(v>>(8*i)); }
static void rehash(void) {
 unsigned int c; for (int i=48;i<52;i++) data[i]=0;
 c=crc32_core_final(crc32_core_update(CRC32_INIT,data,length));
 for (int i=0;i<4;i++) data[48+i]=(unsigned char)(c>>(8*i));
}
static int test(void) {
 length=128096; chunk=17; if (run(0)) return 1;
 if (run(1)>=0) return 2;
 length--; if (run(0)>=0) return 3; length+=2; if (run(0)>=0) return 4; length--;
 data[100]^=1; if (run(0)>=0) return 5; data[100]^=1;
 data[42]=1; rehash(); if (run(0)>=0) return 6;
 data[42]=0; length=128104; put32(16,length); put32(36,1);
 for (int i=0;i<8;i++) data[128096+i]=0;
 data[128096]=4; data[128098]=0; data[128100]=(unsigned char)(data[64]^1); data[128101]=data[65];
 rehash(); if (run(0)) return 7;
 data[128096]=3; rehash(); if (run(0)>=0) return 8;
 data[128096]=4; data[128100]=data[64]; rehash(); if (run(0)>=0) return 9;
 return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
'''


def run_loader(td, data, expected):
    import host32
    src = Path(os.environ.get('OSRI_TEST_SOURCE', ROOT / 'gfx/gfx_osri.c')).read_text()
    wrapper = td / 'wrap.c'; wrapper.write_text(WRAPPER)
    blob = td / 'data.bin'; blob.write_bytes(data + b'X' + bytes(1600))
    expected_blob = td / 'expected.bin'; expected_blob.write_bytes(expected)
    asm = td / 'data.S'; asm.write_text('.data\n.global data,expected_planes\ndata: .incbin "' + str(blob) + '"\nexpected_planes: .incbin "' + str(expected_blob) + '"\n')
    exe = td / 'test'
    includes = ['-I' + str(ROOT / x) for x in ('include', 'gfx', 'fs', 'sdk/include/os32', '.', 'exec')]
    subprocess.run(['gcc', '-m32', '-march=i386', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-std=gnu11', *includes, '-x', 'c', '-', str(wrapper), '-x', 'assembler', str(asm),
                    '-o', str(exe)], input=src, text=True, check=True, cwd=ROOT/'gfx')
    return host32.run([str(exe)], cwd=ROOT, timeout=60).returncode


def check():
    spec = importlib.util.spec_from_file_location('img2osri', os.environ.get('OSRI_CONVERTER_SOURCE', ROOT/'tools/img2osri.py'))
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
    for option, value in (('--events', '101'), ('--per-line', '2')):
        result = subprocess.run([sys.executable, str(ROOT/'tools/img2osri.py'), 'missing.png',
                                 '-o', 'unused.osri', '--preview', 'unused.png', option, value],
                                capture_output=True, text=True)
        assert result.returncode != 0 and 'invalid event' in result.stderr
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        src = Image.new('RGB', (640, 400))
        pix = src.load()
        for y in range(400):
            for x in range(640):
                n = (x + 3 * y + x // 8) % 16
                pix[x, y] = (255 if n & 1 else 0, (n // 4) * 85, 255 if n & 2 else 0)
        data, crc = mod.convert(src)
        assert len(data) == 128096 and struct.unpack_from('<I', data, 48)[0] == crc
        zero = bytearray(data); zero[48:52] = bytes(4)
        assert zlib.crc32(zero) == crc
        output = td/'image.osri'; output.write_bytes(data)
        mod.preview(output, src, td/'preview.png')
        assert Image.open(td/'preview.png').size == (1280, 420)
        # Derive palette indices from the generated source pixels, then pack each
        # bit in display order independently of the converter and loader.
        pal = struct.unpack_from('<16H', data, 64)
        assert len(set(pal)) == 16
        expected = bytearray(128000)
        for y in range(400):
            for x in range(640):
                r, g, b = pix[x, y]
                color = ((r // 17) << 8) | ((g // 17) << 4) | (b // 17)
                index = pal.index(color)
                for p in range(4):
                    if index & (1 << p):
                        expected[p * 32000 + y * 80 + x // 8] += 2 ** (7 - x % 8)
        assert run_loader(td, data, expected) == 0

        # A gradient contains intermediate colors and exceeds the 16-color palette.
        middle = Image.new('RGB', (640, 400))
        middle.putdata([((x * 7 + y) % 256, (x + y * 3) % 256, (x * 3 + y * 5) % 256)
                        for y in range(400) for x in range(640)])
        dithered, _ = mod.convert(middle)
        original = (ROOT/'tools/img2osri.py').read_text()
        anchor = 'd = rgb[c] - palette[index][c]'
        assert original.count(anchor) == 1
        no_dither_path = td/'no_dither.py'
        no_dither_path.write_text(original.replace(anchor, 'd = 0'))
        no_spec = importlib.util.spec_from_file_location('no_dither', no_dither_path)
        no_mod = importlib.util.module_from_spec(no_spec); no_spec.loader.exec_module(no_mod)
        undithered, _ = no_mod.convert(middle)
        assert dithered[96:128096] != undithered[96:128096], 'error diffusion has no effect'
        # Vertical and horizontal gradients plus colored blocks exercise band changes.
        pattern = Image.new('RGB', (640, 400))
        pattern.putdata([((y * 255 // 399), (x * 255 // 639),
                          ((y // 20) * 41 + (x // 160) * 37) % 256)
                         for y in range(400) for x in range(640)])
        _, limited_events, _ = mod.palette_history(pattern, 'B', 3, 2, 4, 8)
        assert len(limited_events) <= 3
        dynamic, _ = mod.convert(pattern, 'B', 100, 1, 4, 8)
        count = struct.unpack_from('<I', dynamic, 36)[0]
        assert 0 < count <= 100
        assert len(dynamic) == 128096 + count * 8
        initial, expected_events, history = mod.palette_history(pattern, 'B', 100, 1, 4, 8)
        events = [struct.unpack_from('<HBBHH', dynamic, 128096 + 8 * i) for i in range(count)]
        assert [(y, i, color) for y, i, _, color, _ in events] == expected_events
        current = list(initial)
        previous = -2
        for y, index, reserved, color, reserved2 in events:
            assert y % 4 == 0 and y - previous >= 2 and not reserved and not reserved2
            assert current[index] != color
            current[index] = color
            previous = y
        output.write_bytes(dynamic)
        mod.preview(output, pattern, td/'dynamic.png')
        preview = Image.open(td/'dynamic.png').load()
        pal = list(initial)
        event_index = 0
        dynamic_error = fixed_error = 0
        fixed_data, _ = mod.convert(pattern)
        fixed_pal = [mod.expand(c) for c in struct.unpack_from('<16H', fixed_data, 64)]
        pattern_pixels = pattern.load()
        for y in range(400):
            while event_index < count and events[event_index][0] == y:
                _, i, _, color, _ = events[event_index]
                pal[i] = color
                event_index += 1
            assert pal == history[y]
            for x in range(640):
                off = 96 + y * 80 + x // 8
                mask = 128 >> (x % 8)
                index = sum(1 << p for p in range(4) if dynamic[off + p * 32000] & mask)
                fixed_index = sum(1 << p for p in range(4) if fixed_data[off + p * 32000] & mask)
                rgb = mod.expand(pal[index])
                assert preview[x, y + 20] == rgb
                original_rgb = pattern_pixels[x, y]
                dynamic_error += sum((rgb[k] - original_rgb[k]) ** 2 for k in range(3))
                fixed_error += sum((fixed_pal[fixed_index][k] - original_rgb[k]) ** 2 for k in range(3))
        assert dynamic_error < fixed_error
        # The global variance must drive both palette selection and distance.
        biased = Image.new('RGB', (640, 400))
        biased.putdata([((x % 16) * 17, (y % 4) * 17, 0)
                        for y in range(400) for x in range(640)])
        weights = mod.variance_weights(biased)
        assert weights[0] > weights[1] > weights[2] == 0
        old_colors = mod.quantized_colors(biased, 16)
        new_colors = mod.variance_colors(biased, 16, weights)
        assert len({c >> 8 & 15 for c in new_colors}) > len({c >> 8 & 15 for c in old_colors})
        assert len(set(new_colors)) == len(new_colors) <= 16
        assert mod.color_distance((17, 0, 0), (0, 17, 0), weights) == 289 * (weights[0] + weights[1])
        for mode in ('A', 'B', 'C'):
            variance, _ = mod.convert(biased, mode, 3, 1, 4, 8, 'variance')
            repeated, _ = mod.convert(biased, mode, 3, 1, 4, 8, 'variance')
            assert variance == repeated
            count = struct.unpack_from('<I', variance, 36)[0]
            assert len(variance) == 128096 + 8 * count and count <= 3
            events = [struct.unpack_from('<HBBHH', variance, 128096 + i * 8) for i in range(count)]
            previous = 0
            palette = list(struct.unpack_from('<16H', variance, 64))
            for y, index, reserved, color, reserved2 in events:
                assert y % 4 == 0 and y - previous >= 2 and index < 16
                assert not reserved and not reserved2 and color < 4096 and palette[index] != color
                if mode == 'B':
                    assert index >= 8
                palette[index] = color
                previous = y
        print('OSRI conversion and loader: GREEN')


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args(); check()
    if args.mutate:
        mutations = [
            ('variance-weights', 'tools/img2osri.py', 'return weights if any(weights) else [1, 1, 1]', 'return [1, 1, 1]', 'OSRI_CONVERTER_SOURCE'),
            ('variance-distance', 'tools/img2osri.py', 'weights[k] * (a[k] - b[k]) ** 2', '(a[k] - b[k]) ** 2', 'OSRI_CONVERTER_SOURCE'),
            ('variance-split', 'tools/img2osri.py', '* weights[channel]', '* (1 if weights[channel] else 0)', 'OSRI_CONVERTER_SOURCE'),
            ('crc', 'gfx/gfx_osri.c', 'crc32_core_final(crc) != expected', '0', 'OSRI_TEST_SOURCE'),
            ('reserved', 'gfx/gfx_osri.c', 'le_read16(h + 42)', '0', 'OSRI_TEST_SOURCE'),
            ('end', 'gfx/gfx_osri.c', 'vfs_read_fd(fd, &extra, 1) != 0', '0', 'OSRI_TEST_SOURCE'),
            ('planes', 'gfx/gfx_osri.c', 'dst->planes[i], plane, &crc', 'dst->planes[i ^ 1], plane, &crc', 'OSRI_TEST_SOURCE'),
            ('bit-order', 'tools/img2osri.py', 'planes[p * PLANE + offset] |= 0x80 >> (x & 7)', 'planes[p * PLANE + offset] |= 1 << (x & 7)', 'OSRI_CONVERTER_SOURCE'),
            ('budget', 'tools/img2osri.py', 'if y and len(events) < events_limit and y - last_line >= 2:', 'if y and (events_limit := 200) and y - last_line >= 2:', 'OSRI_CONVERTER_SOURCE'),
            ('same-color', 'tools/img2osri.py', 'events.append((y, index, color))', 'events.append((y, index, initial[index]))', 'OSRI_CONVERTER_SOURCE'),
            ('band', 'tools/img2osri.py', 'range(0, H, band)', 'range(0, H, 1)', 'OSRI_CONVERTER_SOURCE'),
            ('diffusion', 'tools/img2osri.py', 'd = rgb[c] - palette[index][c]', 'd = 0', 'OSRI_CONVERTER_SOURCE'),
        ]
        with tempfile.TemporaryDirectory() as td:
            for name, source, old, new, variable in mutations:
                src = (ROOT/source).read_text()
                assert src.count(old) == 1, name
                path = Path(td)/Path(source).name; path.write_text(src.replace(old, new))
                env = dict(os.environ, **{variable: str(path)})
                result = subprocess.run([sys.executable, '-B', __file__], env=env, capture_output=True)
                print(name, 'RED' if result.returncode else 'SURVIVED',
                      result.stderr.decode().strip().splitlines()[-1] if result.stderr else '', flush=True)
                assert result.returncode != 0
                assert b'AssertionError' in result.stderr
    return 0

if __name__ == '__main__': sys.exit(main())


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = [sys.executable, '-B', 'tools/tests/test_osri.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, env=env)
