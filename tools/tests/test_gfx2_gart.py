#!/usr/bin/env python3
"""Exercise C6 directly with a freestanding fake allocator and backend."""
CHECK = dict(name='gfx2-gart-host', tier=1, needs=[], budget=5,
    covers=['gfx/v2/gart.c', 'gfx/v2/gfx2_int.h', 'include/gfx_hal2.h',
            'include/dma_contract.h', 'mm/pgalloc.h', 'tools/tests/gfx2_gart_host.c',
            'tools/tests/test_gfx2_gart.py', 'tools/tests/host32.py'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    src = work / 'gart.c'
    src.write_text(source)
    exe = work / 'test'
    includes = ['gfx/v2', 'include', 'mm', 'sdk/include/os32', 'arch/x86']
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        *['-I' + str(ROOT / p) for p in includes], str(src),
        str(ROOT / 'tools/tests/gfx2_gart_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/gart.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            for anchor, replacement, expected in [
                ('pgalloc_unpin(gart_pfns[first + pinned], 1, owner);', '(void)owner;', 2),
                ('if (a->be->gart_unbind(a->ctx, s->gpu_off, s->gart_n)) return OS32_ERR_IO;',
                 '(void)a->be->gart_unbind(a->ctx, s->gpu_off, s->gart_n);', 6),
            ]:
                assert source.count(anchor) == 1
                rc = observe(work, source.replace(anchor, replacement))
                assert rc == expected, (anchor, rc)
                print(f'RED {anchor}: runtime rc={rc}')
    print('PASS GART allocation, rollback, quarantine and release')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
