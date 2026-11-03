"""OSRI conversion and legacy loader tests with generated images and VFS stub."""
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
import numpy as np

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
 if (rc == 0 && im.version != (unsigned)(data[4] | data[5] << 8)) return 97;
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
static void put16(int off, unsigned int v) { data[off]=v; data[off+1]=v>>8; }
static void fixture(unsigned int version, unsigned int n, unsigned int band) {
 put16(4, version); put16(44, band); length=128096+8*n;
 put32(16,length); put32(36,n);
 for (unsigned int i=0;i<n;i++) {
  unsigned int off=128096+8*i, line=1+i/3, index=i%3;
  put16(off,line); data[off+2]=index; data[off+3]=0;
  put16(off+4, (data[64+2*index] | data[65+2*index]<<8) ^ (1+(i/3)%2));
  put16(off+6,0);
 }
 rehash();
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

 fixture(2,0,1); if (run(0)) return 10;
 fixture(2,200,1); if (run(0)) return 11;
 fixture(2,201,1); if (run(0)) return 12;
 fixture(2,1197,1); if (run(0)) return 13;
 fixture(2,1198,1); if (run(0)>=0 || pos != 64) return 14;
 fixture(1,200,1); if (run(0)) return 15;
 fixture(1,201,1); if (run(0)>=0) return 16;
 fixture(1,0,4); if (run(0)) return 17;
 fixture(1,0,1); if (run(0)>=0) return 18;
 fixture(1,4,1); put16(128096+24,1); data[128096+26]=3;
 put16(128096+28,(data[70] | data[71]<<8)^1); rehash(); if (run(0)) return 19;
 put16(4,2); rehash(); if (run(0)>=0) return 20;
 fixture(2,1,1); put16(128096,0); rehash(); if (run(0)>=0) return 21;
 put16(128096,400); rehash(); if (run(0)>=0) return 22;
 put16(128096,1); data[128098]=16; rehash(); if (run(0)>=0) return 23;
 fixture(2,2,1); data[128106]=0; rehash(); if (run(0)>=0) return 24;
 fixture(2,1,1); put16(128100,data[64] | data[65]<<8); rehash(); if (run(0)>=0) return 25;
 fixture(3,0,1); if (run(0)>=0) return 26;
 return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
'''


def run_loader(td, data, expected):
    import host32
    # Keep the existing C loader cases on v1 independently of the encoder default.
    data = bytearray(data)
    struct.pack_into('<H', data, 4, 1)
    struct.pack_into('<H', data, 44, 4)
    data[48:52] = bytes(4)
    struct.pack_into('<I', data, 48, zlib.crc32(data))
    src = Path(os.environ.get('OSRI_TEST_SOURCE', ROOT / 'gfx/gfx_osri.c')).read_text()
    wrapper = td / 'wrap.c'; wrapper.write_text(WRAPPER)
    blob = td / 'data.bin'; blob.write_bytes(data + b'X' + bytes(10000))
    expected_blob = td / 'expected.bin'; expected_blob.write_bytes(expected)
    asm = td / 'data.S'; asm.write_text('.data\n.global data,expected_planes\ndata: .incbin "' + str(blob) + '"\nexpected_planes: .incbin "' + str(expected_blob) + '"\n')
    exe = td / 'test'
    includes = ['-I' + str(ROOT / x) for x in ('include', 'gfx', 'fs', 'sdk/include/os32', '.', 'exec')]
    subprocess.run(['gcc', '-m32', '-march=i386', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-std=gnu11', *includes, '-x', 'c', '-', str(wrapper), '-x', 'assembler', str(asm),
                    '-o', str(exe)], input=src, text=True, check=True, cwd=ROOT/'gfx')
    return host32.run([str(exe)], cwd=ROOT, timeout=60).returncode


def check_small(mod):
    # Expected events were generated with pl_estimate.py using these same
    # RGB444-quantized row palettes and Pillow's MEDIANCUT input.
    pixels = np.array([
        [[22,239,236],[24,75,188],[230,189,253],[74,49,139],[137,112,240],[192,237,77],[106,110,94],[18,22,119]],
        [[220,131,68],[29,58,185],[141,26,180],[17,33,20],[83,154,131],[247,110,23],[167,91,106],[177,28,82]],
        [[81,35,16],[215,180,237],[72,51,10],[105,133,109],[49,27,150],[219,167,233],[138,34,205],[138,93,131]],
        [[191,41,134],[115,29,173],[226,45,83],[194,234,74],[77,114,30],[192,155,110],[106,215,224],[158,0,203]],
    ], dtype=np.uint8)
    image = Image.fromarray(pixels)
    old_constants = mod.W, mod.H, mod.PLANE, mod.EVENTS_OFF
    try:
        mod.W, mod.H, mod.PLANE, mod.EVENTS_OFF = 8, 4, 4, 112
        initial, events, history = mod.palette_history(image)
        assert initial == [279,331,494,1080,1638,2174,3045,3775] + [0]*8
        assert events == [(1,5,2091),(1,6,2900),(2,0,2997),(2,3,1073),(3,2,1758),(3,4,3382)]
        for before, after in zip(history, history[1:]):
            changes = [(a,b) for a,b in zip(before,after) if a != b]
            cost = sum(sum((a >> shift & 15) != (b >> shift & 15)
                           for shift in (0,4,8)) for a,b in changes) + max(0,len(changes)-1)
            unpack = lambda row: np.array([[(c >> shift) & 15 for shift in (8,4,0)]
                                           for c in row], dtype=np.uint8)
            assert mod.out_count(unpack(before), unpack(after)) == cost <= 5
        two_initial, two_band_events, two_history = mod.palette_history(image, band=2)
        assert two_band_events and all(event[0] == 2 for event in two_band_events)
        packed, crc = mod.convert(image, band=2)
        assert struct.unpack_from('<H', packed, 4)[0] == 2
        assert struct.unpack_from('<H', packed, 44)[0] == 2
        assert struct.unpack_from('<I', packed, 48)[0] == crc
        zeroed = bytearray(packed); zeroed[48:52] = bytes(4)
        assert zlib.crc32(zeroed) == crc
        serialized = [struct.unpack_from('<HBBHH', packed, 112 + 8*i)
                      for i in range(struct.unpack_from('<I', packed, 36)[0])]
        assert serialized == [(y, i, 0, color, 0) for y, i, color in two_band_events]
        current = two_initial.copy()
        for y in range(4):
            for line, index, color in two_band_events:
                if line == y: current[index] = color
            assert current == two_history[y]
        assert all(a[:2] < b[:2] for a, b in zip(serialized, serialized[1:]))
        initial6, events6, history6 = mod.palette_history(image, hblank_outs=6)
        current = initial6.copy()
        for y in range(4):
            for line, index, color in events6:
                if line == y: current[index] = color
            assert current == history6[y]
        assert events6 and any(a != b for a, b in zip(history6, history6[1:]))
        zero = np.zeros((16, 3), dtype=np.uint8)
        for changes, expected in (([(0,0)],1), ([(0,0),(0,1),(0,2)],3),
                                  ([(0,0),(1,0),(2,0)],5),
                                  ([(0,0),(0,1),(1,0),(2,0)],6),
                                  ([(0,0),(0,1),(0,2),(1,0),(1,1),(1,2)],7)):
            changed = zero.copy()
            for slot, component in changes: changed[slot, component] = 1
            assert mod.out_count(zero, changed) == expected
            if expected == 7: assert mod.out_count(zero, changed) > 6
        data, _ = mod.convert(image)
        # Independent physical-index reconstruction, including signed FS carry.
        error = [[0]*3 for _ in range(10)]
        for y in range(4):
            nxt = [[0]*3 for _ in range(10)]
            colors = [[(c >> shift & 15)*17 for shift in (8,4,0)] for c in history[y]]
            for x in range(8):
                value = [max(0,min(255,int(pixels[y,x,c])+error[x+1][c])) for c in range(3)]
                index = min(range(16), key=lambda i: sum((value[c]-colors[i][c])**2 for c in range(3)))
                decoded = sum(((data[96+plane*4+y] >> (7-x)) & 1) << plane for plane in range(4))
                assert decoded == index
                for c in range(3):
                    d = value[c]-colors[index][c]
                    for target, pos, weight in ((error,x+2,7),(nxt,x,3),(nxt,x+1,5),(nxt,x+2,1)):
                        v = d*weight
                        target[pos][c] += (abs(v)//16) * (1 if v >= 0 else -1)
            error = nxt
        # Expected indices were calculated by hand from the signed FS recurrence.
        # This input changes four physical indices if vertical FS carry is lost.
        carry_image = Image.fromarray(np.random.default_rng(0).integers(0, 256, (4,8,3), dtype=np.uint8))
        carry_data, _ = mod.convert(carry_image)
        for y, x, expected_index in ((2,3,5),(2,4,4),(3,2,7),(3,6,3)):
            decoded = sum(((carry_data[96+p*4+y] >> (7-x)) & 1) << p for p in range(4))
            assert decoded == expected_index
        # Narrow full-height fixture checks empty rows, prefetch, and row 399
        # without repeatedly converting a 640-pixel-wide picture.
        mod.W, mod.H, mod.PLANE, mod.EVENTS_OFF = 8, 400, 400, 1696
        tall = Image.new('RGB', (8, 400), (0,0,0))
        for x in range(8):
            tall.putpixel((x,399), (255,0,0))
        tall_data, _ = mod.convert(tall)
        assert struct.unpack_from('<I', tall_data, 36)[0] == 1
        assert struct.unpack_from('<HBBHH', tall_data, 1696) == (398,0,0,0xf00,0)
        assert tall_data[96+398] == 255 and tall_data[96+399] == 0
        assert all(tall_data[96+p*400+399] == 0 for p in range(4))
        mod.W, mod.H, mod.PLANE, mod.EVENTS_OFF = 8, 4, 4, 112
        # Expected slot-tie output was generated with pl_estimate.py.
        # Equal slot scores with different gains and costs: argmax keeps the
        # first RGB proposal; using gain as an intra-slot tiebreak changes it.
        old = np.array([[1,2,2],[2,0,1],[1,0,2],[1,1,1],[1,1,0],[1,2,1],
                        [1,0,2],[0,1,1],[1,1,2],[2,2,2],[2,1,1],[0,0,2],
                        [2,2,2],[1,2,1],[1,0,2],[2,1,0]], dtype=np.uint8)
        pts = np.array([[[34,0,17],[34,17,0],[0,34,0],[0,0,17],[0,0,34]],
                        [[0,34,0],[0,0,34],[17,34,34],[0,0,17],[34,17,0]]], dtype=np.uint8)
        ends = np.array([[[0,1,0],[1,0,2],[0,0,2],[0,1,1]],
                         [[0,2,0],[0,0,0],[0,0,1],[0,0,0]]], dtype=np.uint8)
        expected = old.copy(); expected[5] = [0,2,0]; expected[7] = [0,0,1]
        mod.W = 5
        assert np.array_equal(mod.optimize_row(old, pts, ends, 5), expected)
        # Lookahead accepts a current-row loss of 289 to save 1156 on the next row.
        old = np.array([[2,0,0],[1,2,2],[2,0,1],[0,0,1],[2,0,2],[0,0,2],[0,2,2],[1,1,0],
                        [2,1,0],[0,1,1],[0,1,2],[0,2,0],[2,0,2],[1,1,0],[1,0,0],[0,2,2]], dtype=np.uint8)
        pts = np.array([[[34,34,34],[0,0,34],[17,34,17],[0,17,34],[0,0,34]],
                        [[34,34,0],[17,34,34],[0,34,17],[0,34,17],[0,17,34]]], dtype=np.uint8)
        ends = np.array([[[0,1,2],[0,2,1],[2,0,2],[0,1,0]],
                         [[2,2,1],[1,2,2],[2,0,1],[1,1,2]]], dtype=np.uint8)
        planned = mod.optimize_row(old, pts, ends, 5)
        losses = [int(mod.distances(row, planned * 17).min(axis=1).sum()) for row in pts]
        assert losses == [289, 0]  # no lookahead yields [0, 1156]
        assert losses[0] > 0 and losses[0] + losses[1] / 2 < 1156 / 2
        mod.W = 8
        flat = Image.new('RGB', (8, 1)); flat.putdata([(1,1,1),(2,2,2)]*4)
        assert mod.quantized_colors(flat) == [0]*16
    finally:
        mod.W, mod.H, mod.PLANE, mod.EVENTS_OFF = old_constants


def check():
    spec = importlib.util.spec_from_file_location('img2osri', os.environ.get('OSRI_CONVERTER_SOURCE', ROOT/'tools/img2osri.py'))
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
    check_small(mod)
    for option, value in (('--hblank-outs', '7'), ('--band', '3')):
        result = subprocess.run([sys.executable, str(ROOT/'tools/img2osri.py'), 'missing.png',
                                 '-o', 'unused.osri', option, value],
                                capture_output=True, text=True)
        assert result.returncode != 0 and 'invalid choice' in result.stderr
    with tempfile.TemporaryDirectory() as name:
        td = Path(name)
        # A static v1 file with independently chosen plane bytes and palette.
        planes = bytes((i * 37 + i // 80) & 255 for i in range(128000))
        header = struct.pack('<4sHHHHBBHIIIIIIHHHHIIII', b'OSRI', 1, 64,
                             640, 400, 4, 1, 0, 128096, 64, 96, 128000,
                             128096, 0, 8, 0, 4, 0, 0, 0, 0, 0)
        data = bytearray(header + struct.pack('<16H', *range(16)) + planes)
        struct.pack_into('<I', data, 48, zlib.crc32(data))
        assert run_loader(td, data, planes) == 0
    print('OSRI conversion and loader: GREEN')


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args(); check()
    if args.mutate:
        mutations = [
            ('v1-line-limit', 'gfx/gfx_osri.c', 'version == OSRI_VERSION_2 && line_count > OSRI_V2_MAX_EVENTS_PER_LINE', 'line_count > OSRI_V2_MAX_EVENTS_PER_LINE', 'OSRI_TEST_SOURCE'),
            ('v2-max-plus-one', 'gfx/gfx_osri.c', 'OSRI_V1_MAX_EVENTS : OSRI_V2_MAX_EVENTS)', 'OSRI_V1_MAX_EVENTS : OSRI_V2_MAX_EVENTS + 1)', 'OSRI_TEST_SOURCE'),
            ('v2-count', 'gfx/gfx_osri.c', 'version == OSRI_VERSION_1 ? OSRI_V1_MAX_EVENTS : OSRI_V2_MAX_EVENTS', 'OSRI_V1_MAX_EVENTS', 'OSRI_TEST_SOURCE'),
            ('version', 'gfx/gfx_osri.c', '(version != OSRI_VERSION_1 && version != OSRI_VERSION_2)', '0', 'OSRI_TEST_SOURCE'),
            ('crc', 'gfx/gfx_osri.c', 'crc32_core_final(crc) != expected', '0', 'OSRI_TEST_SOURCE'),
            ('reserved', 'gfx/gfx_osri.c', 'le_read16(h + 42)', '0', 'OSRI_TEST_SOURCE'),
            ('end', 'gfx/gfx_osri.c', 'vfs_read_fd(fd, &extra, 1) != 0', '0', 'OSRI_TEST_SOURCE'),
            ('planes', 'gfx/gfx_osri.c', 'dst->planes[i], plane, &crc', 'dst->planes[i ^ 1], plane, &crc', 'OSRI_TEST_SOURCE'),
            ('converter-crc', 'tools/img2osri.py', "struct.pack_into('<I', data, 48, crc)", 'pass', 'OSRI_CONVERTER_SOURCE'),
            ('converter-version', 'tools/img2osri.py', "b'OSRI', 2, 64, W, H", "b'OSRI', 1, 64, W, H", 'OSRI_CONVERTER_SOURCE'),
            ('vertical-carry', 'tools/img2osri.py', 'error = nxt', 'error = [[0, 0, 0] for _ in range(W + 2)]', 'OSRI_CONVERTER_SOURCE'),
            ('slot-tie', 'tools/img2osri.py', 'k = int(np.argmax(scores))', 'k = max(range(len(scores)), key=lambda i: (scores[i], gain[i]))', 'OSRI_CONVERTER_SOURCE'),
            ('lookahead', 'tools/img2osri.py', 'weights[band * W:] = 0.5', 'weights[band * W:] = 0', 'OSRI_CONVERTER_SOURCE'),
            ('fixed-slots', 'tools/img2osri.py', 'for slot in range(16):', 'for slot in range(1):', 'OSRI_CONVERTER_SOURCE'),
            ('delete-events', 'tools/img2osri.py', 'events.append((y, index, color))', 'pass', 'OSRI_CONVERTER_SOURCE'),
            ('all-components', 'tools/img2osri.py', 'MASKS[:, None, :], candidates[None, :, :]', 'np.ones_like(MASKS[:, None, :]), candidates[None, :, :]', 'OSRI_CONVERTER_SOURCE'),
            ('index-cost', 'tools/img2osri.py', 'np.maximum(0, slots_other + (changed > 0) - 1)', '0', 'OSRI_CONVERTER_SOURCE'),
            ('bit-order', 'tools/img2osri.py', 'planes[p * PLANE + offset] |= 0x80 >> (x & 7)', 'planes[p * PLANE + offset] |= 1 << (x & 7)', 'OSRI_CONVERTER_SOURCE'),
            ('same-color', 'tools/img2osri.py', 'events.append((y, index, color))', 'events.append((y, index, initial[index]))', 'OSRI_CONVERTER_SOURCE'),
            ('band', 'tools/img2osri.py', 'for y in range(band, H, band):', 'for y in range(1, H, 1):', 'OSRI_CONVERTER_SOURCE'),
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
