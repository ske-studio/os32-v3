"""G1 DIRECT eligibility, CPU pixel transfers and interruption boundaries."""
CHECK = dict(name='gf-direct', tier=1, needs=[], budget=8,
             covers=['drivers/video/geforce/gf_direct.c',
                     'tools/tests/test_gf_direct.py', 'tools/tests/gf_direct_host.c',
                     'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
                     'drivers/video/geforce/geforce_module.h', 'hwdev/gf/**',
                     'hwdev/gfinit_mode.h', 'include/**', 'gfx/gfx.h',
                     'kernel/module/*.h', 'build/config.mk'])
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('map-bound', 'fb->bytes <= GF_MODE_VRAM_BYTES - off', '1'),
    ('format', 'fb.format == GFX_FMT_PACKED8', '1'),
    ('width', 'width == GF_MODE_WIDTH', '1'),
    ('height', 'height == GF_MODE_HEIGHT', '1'),
    ('key', 'if (*s != (u8)key) *d = *s;', '*d = *s;'),
    ('dword', '*(volatile u32 *)d = value;', '*(volatile u32 *)d = value ^ 1U;'),
    ('tail', 'while (n) { *d++ = *s++; --n; }', 'while (n) { *d++ = 0; ++s; --n; }'),
    ('source-pitch', 'src += pitch;', 'src += (u32)w;'),
    ('get-pitch', 'dst += pitch;', 'dst += (u32)w;'),
    ('get-pixel', 'dst[col] = src[col]', 'dst[col] = 0'),
    ('put-point', 'rc = p->point(p->opaque);\n        if (rc) return rc;\n        const u8 *s',
     'rc = 0;\n        if (rc) return rc;\n        const u8 *s'),
    ('drain', 'volatile u8 *bar0 = P2V_IO(p->bar0_phys);\n'
     '    (void)*(volatile u32 *)(bar0 + GF_DIRECT_PFB_AMOUNT);', ''),
]


def run_case(folder, mutation=None):
    path = ROOT/'drivers/video/geforce/gf_direct.c'
    source = path.read_text()
    if mutation:
        label, old, new = mutation
        assert source.count(old) == 1, label
        source = source.replace(old, new)
    source = re.sub(r'#include "([^"\n]+)"',
                    lambda m: '#include "'+str((path.parent/m[1]).resolve())+'"'
                    if (path.parent/m[1]).exists() else m[0], source)
    product = folder/'direct.c'
    product.write_text(source)
    fixture = (ROOT/'tools/tests/gf_direct_host.c').read_text().replace(
        '"DIRECT_SOURCE"', '"'+str(product)+'"')
    host = folder/'host.c'
    host.write_text(fixture)
    exe = folder/'host'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                    '-fno-stack-protector', '-D__KERNEL_BUILD__',
                    *include_flags(ROOT), '-I'+str(ROOT/'drivers/video/geforce'),
                    '-I'+str(ROOT), str(host), '-nostdlib', '-static', '-no-pie',
                    '-o', str(exe)], check=True)
    result = host32.run([str(exe)], capture_output=True, timeout=10)
    host32.report_signal(result.returncode)
    assert result.returncode == (1 if mutation else 0), (mutation, result)
    print('runtime RED: '+mutation[0] if mutation else 'GREEN: gf-direct', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf-direct-') as td:
        folder = Path(td)
        run_case(folder)
        if args.mutate:
            for mutation in MUTATIONS:
                run_case(folder, mutation)
                run_case(folder)


def run(ctx):
    argv = [sys.executable, '-B', 'tools/tests/test_gf_direct.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)


if __name__ == '__main__':
    main()
