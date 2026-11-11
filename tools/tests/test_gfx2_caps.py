#!/usr/bin/env python3
"""C10: compile real capability code freestanding; reject runtime mutants."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'gfx/v2/caps.c'
MUTATIONS = [
    ('size-cap', 'size < sizeof(c) ? size : sizeof(c)', 'sizeof(c)'),
    ('sw-ops', 'GFX2_CAP2D(GFX2_OP_MAX + 1) - 2', 'GFX2_CAP2D(GFX2_OP_MAX) - 2'),
    ('sync', 'c.cpu_direct = 1;', 'c.cpu_direct = 0;'),
    ('profile', 'gfxsync_profile(ctx, c.sync_profile);', '(void)ctx;'),
    ('refresh', 'if (!a->be->caps(a->ctx, &c)) a->caps = c;', 'if (a->be->caps(a->ctx, &c)) a->caps = c;'),
]


def observe(work, source):
    src = work / 'caps.c'
    src.write_text(source)
    exe = work / 'shim'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
           '-Werror', '-ffreestanding', '-fno-pie',
           '-fno-stack-protector', '-nostdlib', '-static', '-no-pie']
    cmd += ['-I' + str(ROOT / p) for p in
            ('gfx/v2', 'lib', 'include', 'sdk/include/os32', 'mm', 'arch/x86', 'platform/pc98')]
    host32.build(cmd + [str(src), str(ROOT / 'tools/tests/gfx2_caps_host.c'),
                       '-o', str(exe)], check=True)
    return host32.run([str(exe)], timeout=30).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory(prefix='gfx2-caps-') as td:
        work = Path(td)
        assert observe(work, source) == 0
        print('GREEN C10 caps size, shim capabilities, refresh', flush=True)
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

