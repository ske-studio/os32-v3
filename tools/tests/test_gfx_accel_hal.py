"""H1: real ILP32 declaration layout and inert NOSYS entry points."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

CHECK = dict(name='gfx-accel-hal-host', tier=1, needs=[], budget=10, covers=[
    'include/gfx_hal.h', 'gfx/gfx_hw_submit.c', 'gfx/gfx.h',
    'include/gfx_mode.h', 'include/gfx_format.h', 'include/types.h',
    'include/pc98.h', 'include/memmap.h', 'exec/surface_limits.h',
    'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_kapi_slots.h',
    'tools/tests/gfx_accel_hal_host.c', 'tools/tests/test_gfx_accel_hal.py',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('batch-limit', 'sdk/include/os32/os32_kapi_shared.h',
     '#define GFX_CMD_MAX  64', '#define GFX_CMD_MAX  63'),
    ('signed-coordinates', 'sdk/include/os32/os32_kapi_shared.h',
     'i16 x, y, w, h, sx, sy;', 'u16 x, y, w, h, sx, sy;'),
    ('submit-nosys', 'gfx/gfx_hw_submit.c',
     '(void)cmds; (void)n; (void)gen; (void)done;\n    return OS32_ERR_NOSYS;',
     '(void)cmds; (void)n; (void)gen; (void)done;\n    return 0;'),
    ('get-nosys', 'gfx/gfx_hw_submit.c',
     '(void)dst; (void)pitch; (void)gen;\n    return OS32_ERR_NOSYS;',
     '(void)dst; (void)pitch; (void)gen;\n    return 0;'),
)


def run_case(folder, runner, mutation=None):
    edits = {}
    if mutation:
        name, path, old, new = mutation
        source = (ROOT / path).read_text()
        assert source.count(old) == 1, name
        edits[path] = source.replace(old, new)
    exe = folder / 'hal-host'
    command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
               '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
               '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
               *include_flags(ROOT),
               str(ROOT / 'tools/tests/gfx_accel_hal_host.c'),
               str(ROOT / 'gfx/gfx_hw_submit.c'), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), result
    if mutation:
        assert result.stdout.startswith('FAIL '), result
    print(('MUTATE RED ' + mutation[0]) if mutation else 'PASS H1 layout/NOSYS (' + runner + ')')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        for mutation in (None, *MUTATIONS) if args.mutate else (None,):
            with tempfile.TemporaryDirectory(prefix='gfx-accel-hal-') as td:
                run_case(Path(td), runner, mutation)


def run(ctx) -> int:
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_gfx_accel_hal.py', *(['--mutate'] if ctx.mutate else [])]
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
