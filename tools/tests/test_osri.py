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
extern unsigned char data[]; extern unsigned char data_end[];
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


def run_loader(td, data):
    import host32
    src = Path(os.environ.get('OSRI_TEST_SOURCE', ROOT / 'gfx/gfx_osri.c')).read_text()
    wrapper = td / 'wrap.c'; wrapper.write_text(WRAPPER)
    blob = td / 'data.bin'; blob.write_bytes(data + b'X' + bytes(1600))
    asm = td / 'data.S'; asm.write_text('.data\n.global data,data_end\ndata: .incbin "' + str(blob) + '"\ndata_end:\n')
    exe = td / 'test'
    includes = ['-I' + str(ROOT / x) for x in ('include', 'gfx', 'fs', 'sdk/include/os32', '.', 'exec')]
    subprocess.run(['gcc', '-m32', '-march=i386', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-std=gnu11', *includes, '-x', 'c', '-', str(wrapper), '-x', 'assembler', str(asm),
                    '-o', str(exe)], input=src, text=True, check=True, cwd=ROOT/'gfx')
    return host32.run([str(exe)], cwd=ROOT, timeout=60).returncode


def check():
    spec = importlib.util.spec_from_file_location('img2osri', ROOT/'tools/img2osri.py')
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        src = Image.new('RGB', (640, 400))
        pix = src.load()
        for y in range(400):
            for x in range(640): pix[x, y] = (255 if x < 320 else 0, 255 if y < 200 else 0, 0)
        data, crc = mod.convert(src)
        assert len(data) == 128096 and struct.unpack_from('<I', data, 48)[0] == crc
        zero = bytearray(data); zero[48:52] = bytes(4)
        assert zlib.crc32(zero) == crc
        output = td/'image.osri'; output.write_bytes(data)
        mod.preview(output, src, td/'preview.png')
        assert Image.open(td/'preview.png').size == (1280, 420)
        assert run_loader(td, data) == 0
        print('OSRI conversion and loader: GREEN')


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args(); check()
    if args.mutate:
        src = (ROOT/'gfx/gfx_osri.c').read_text()
        mutations = [('crc', 'crc32_core_final(crc) != expected', '0'),
                     ('reserved', 'le_read16(h + 42)', '0'),
                     ('end', 'vfs_read_fd(fd, &extra, 1) != 0', '0')]
        with tempfile.TemporaryDirectory() as td:
            for name, old, new in mutations:
                assert src.count(old) == 1
                path = Path(td)/'gfx_osri.c'; path.write_text(src.replace(old, new))
                env = dict(os.environ, OSRI_TEST_SOURCE=str(path))
                result = subprocess.run([sys.executable, '-B', __file__], env=env, capture_output=True)
                print(name, 'RED' if result.returncode else 'SURVIVED')
                assert result.returncode != 0
    return 0

if __name__ == '__main__': sys.exit(main())


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = [sys.executable, '-B', 'tools/tests/test_osri.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, env=env)
