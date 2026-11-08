"""H3: clipped mono expansion into synchronous bounded puts."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

CHECK = dict(name='gfx-hw-mono-cpu-host', tier=1, needs=[], budget=10, covers=[
    'gfx/gfx_hw_mono_cpu.c', 'gfx/gfx_hw_mono_cpu.h', 'include/gfx_hal.h',
    'tools/tests/gfx_hw_mono_cpu_host.c', 'tools/tests/test_gfx_hw_mono_cpu.py',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('bit-order', '0x80U >> (bit & 7)', '1U << (bit & 7)'),
    ('transparent-key', '(fg ^ 1) : -1', '(fg ^ 2) : -1'),
    ('opaque-background', '(u8)key : (u8)bg', '(u8)key : fg'),
    ('clip-left', 'if (left < cx) left = cx;', 'if (left < cx) left = cx + 1;'),
    ('put-error', 'if (rc < 0) return rc;', 'if (rc < 0) return 0;'),
)

def run_case(folder, runner, mutation=None):
    edits = {}
    if mutation:
        name, old, new = mutation
        source = (ROOT / 'gfx/gfx_hw_mono_cpu.c').read_text()
        assert source.count(old) == 1, name
        edits['gfx/gfx_hw_mono_cpu.c'] = source.replace(old, new)
    exe = folder / 'mono-host'
    command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
               '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
               '-static', '-no-pie', *include_flags(ROOT),
               str(ROOT / 'tools/tests/gfx_hw_mono_cpu_host.c'),
               str(ROOT / 'gfx/gfx_hw_mono_cpu.c'), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), result
    if mutation:
        assert result.stdout.startswith('FAIL '), result
    print('MUTATE RED ' + mutation[0] if mutation else 'PASS H3 mono (' + runner + ')')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        for mutation in (None, *MUTATIONS, None) if args.mutate else (None,):
            with tempfile.TemporaryDirectory(prefix='gfx-mono-') as td:
                run_case(Path(td), runner, mutation)

def run(ctx) -> int:
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx_hw_mono_cpu.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env, close_fds=False)

if __name__ == '__main__':
    main()
