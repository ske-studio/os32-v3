#!/usr/bin/env python3
"""Test GeForce v2 lifecycle with a freestanding fake channel."""
CHECK = dict(name='gf-v2-life-host', tier=1, needs=[], budget=5,
    covers=['drivers/video/geforce/gf_v2_life.c', 'drivers/video/geforce/gf_v2_fence.c',
            'drivers/video/geforce/gf_v2.h', 'drivers/video/geforce/backend_geforce.c', 'tools/tests/gf_v2_life_host.c',
            'tools/tests/test_gf_v2_life.py', 'tools/tests/host32.py'])
from pathlib import Path
import argparse
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source, fence_source):
    src = work / 'life.c'
    src.write_text(source)
    fence = work / 'fence.c'
    fence.write_text(fence_source)
    exe = work / 'test'
    includes = ['.', 'include', 'sdk/include/os32', 'arch/x86', 'mm', 'drivers/video/geforce']
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        *['-I' + str(ROOT / p) for p in includes], str(src),
        str(fence),
        str(ROOT / 'tools/tests/gf_v2_life_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'drivers/video/geforce/gf_v2_life.c').read_text()
    fence_source = (ROOT / 'drivers/video/geforce/gf_v2_fence.c').read_text()
    backend = (ROOT / 'drivers/video/geforce/backend_geforce.c').read_text()
    backend_anchor = '    (void)gf_v2_idle();'
    assert backend.count(backend_anchor) == 1
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source, fence_source) == 0
        if args.mutate:
            for anchor, replacement in [
                ('*dl = now + us;', '*dl = us;'),
                ('case GF2_ETIME: case GF4_ETIME: return OS32_ERR_TIMEOUT;', 'case GF2_ETIME: case GF4_ETIME: return OS32_ERR_IO;'),
                ('if (!rc) return 0;', 'if (!rc) rc = GF2_ETIME;'),
                ('if (g->epoch != c->ch->epoch) gf_v2_fence_reset(g);', '(void)g->epoch;'),
            ]:
                assert source.count(anchor) == 1
                rc = observe(work, source.replace(anchor, replacement), fence_source)
                assert rc != 0, (anchor, rc)
                print(f'RED {anchor}: runtime rc={rc}')
            for anchor in ['    g->completed = r->e[at].v2;', '    r->tail = r->head; r->count = 0;']:
                assert fence_source.count(anchor) == 1
                rc = observe(work, source, fence_source.replace(anchor, ''))
                assert rc != 0, (anchor, rc)
                print(f'RED {anchor}: runtime rc={rc}')
            assert backend.replace(backend_anchor, '').count(backend_anchor) != 1
            print('RED backend gf_v2_idle: source anchor absent')
    print('PASS GF v2 lifecycle')


if __name__ == '__main__':
    main()


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
