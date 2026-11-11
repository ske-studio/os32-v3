#!/usr/bin/env python3
"""C16: compile real shim tables freestanding; reject runtime mutants."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'gfx/v2/v1_shim.c'
MUTATIONS = [
    ('empty-heap', '*base = *size = *phys = 0;', '*base = *size = *phys = 1;'),
    ('completion', '*done = a->fence.issued;', '*done = a->fence.issued + 1;'),
    ('lost-completion', '*done = *fail = a->fence.issued;', '*done = *fail = a->fence.issued + 1;'),
    ('mode-head', 'return head || !a->v1->set_mode ?', 'return (head > 1) || !a->v1->set_mode ?'),
    ('flip-sync', '!!(flags & GFX2_FLIP_VBLANK)', '!(flags & GFX2_FLIP_VBLANK)'),
    ('palette-red', '(xrgb[i] >> 20) & 15', '(xrgb[i] >> 16) & 15'),
    ('cursor-pitch', '.bytes = s->bytes, .pitch = s->pitch,', '.bytes = s->bytes, .pitch = 0,'),
    ('lost-unbind', 'static int lost_gart_unbind(void *c, u32 off, u32 n)\n{\n    (void)c;\n    (void)off;\n    (void)n;\n    return 0;',
     'static int lost_gart_unbind(void *c, u32 off, u32 n)\n{\n    (void)c;\n    (void)off;\n    (void)n;\n    return OS32_ERR_STALE;'),
    ('unsupported', 'static int shim_check(void *c, const struct gfx2_exec *e)\n{\n    (void)c;\n    (void)e;\n    return OS32_ERR_NOSYS;',
     'static int shim_check(void *c, const struct gfx2_exec *e)\n{\n    (void)c;\n    (void)e;\n    return 0;'),
]


def observe(work, source):
    src = work / 'v1_shim.c'
    src.write_text(source)
    exe = work / 'shim'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
           '-Werror', '-ffreestanding', '-fno-pie',
           '-fno-stack-protector', '-nostdlib', '-static', '-no-pie']
    cmd += ['-I' + str(ROOT / p) for p in
            ('gfx/v2', 'include', 'sdk/include/os32', 'mm', 'arch/x86', 'platform/pc98')]
    host32.build(cmd + [str(src), str(ROOT / 'tools/tests/gfx2_v1_shim_host.c'),
                       '-o', str(exe)], check=True)
    return host32.run([str(exe)], timeout=30).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory(prefix='gfx2-v1-shim-') as td:
        work = Path(td)
        assert observe(work, source) == 0
        print('GREEN C16 all table hooks, conversions, live v1 hooks, detached cleanup', flush=True)
        if args.mutate:
            for name, anchor, replacement in MUTATIONS:
                assert source.count(anchor) == 1, name
                rc = observe(work, source.replace(anchor, replacement))
                assert rc > 0, (name, rc)
                print(f'RED {name}: runtime rc={rc}', flush=True)
            assert observe(work, source) == 0
            print('GREEN restored control', flush=True)


if __name__ == '__main__':
    main()

