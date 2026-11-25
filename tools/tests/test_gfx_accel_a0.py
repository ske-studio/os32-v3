"""A0: shared declaration types and inert foundations under ILP32."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

CHECK = dict(name='gfx-accel-a0-host', tier=1, needs=[], budget=10, covers=[
    'sys/hal/gfx/gfx_policy.h', 'sys/hal/gfx/gfx_policy.c', 'sys/hal/gfx/gfx_hw_mono_cpu.h',
    'sys/hal/gfx/gfx_hw_mono_cpu.c', 'sys/hal/gfx/gfx_internal.h', 'sys/include/gfx_hal.h',
    'sys/dev/video/geforce/geforce_module.h', 'sys/dev/video/geforce/accel.h',
    'userland/lib/gfx/gfx_direct_internal.h', 'userland/lib/gfx/libos32gfx.h',
    'userland/lib/gfx/draw/gfx_cmd.c', 'userland/lib/gfx/draw/gfx_copy.c',
    'tools/tests/gfx_accel_a0_host.c', 'tools/tests/test_gfx_accel_a0.py',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'],
    basis=['GFX_ACCEL_HAL_DESIGN §3'],
)
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('policy-gen-type', 'sys/hal/gfx/gfx_policy.h',
     'extern u16 gfx_policy_gen;', 'extern u32 gfx_policy_gen;'),
    ('client-gen-type', 'userland/lib/gfx/gfx_direct_internal.h',
     'extern u16 gfx_gen;', 'extern u32 gfx_gen;'),
    ('copy-const-backend', 'sys/include/gfx_hal.h',
     'extern GfxBackend *g_backend;', 'extern const GfxBackend *g_backend;'),
    ('direct-ops-const', 'sys/dev/video/geforce/geforce_module.h',
     'extern const GfxDirectOps gf_direct_ops;', 'extern GfxDirectOps gf_direct_ops;'),
)


def run_case(folder, runner, mutation=None):
    edits = {}
    if mutation:
        name, path, old, new = mutation
        source = (ROOT / path).read_text()
        assert source.count(old) == 1, name
        if name == 'policy-gen-type':
            edits['sys/hal/gfx/gfx_policy.c'] = (ROOT / 'sys/hal/gfx/gfx_policy.c').read_text().replace(
                'u16 gfx_policy_gen = 1;', 'u32 gfx_policy_gen = 1;')
        if name == 'copy-const-backend':
            # Keep the linked implementation buildable; TYPE still observes
            # the mutated public declaration at runtime.
            edits['sys/hal/gfx/gfx_policy.c'] = (ROOT / 'sys/hal/gfx/gfx_policy.c').read_text().replace(
                'g_backend', '((GfxBackend *)g_backend)')
        edits[path] = source.replace(old, new)
    exe = folder / 'hal-host'
    command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
               '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
               '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
               *include_flags(ROOT),
               str(ROOT / 'tools/tests/gfx_accel_a0_host.c'),
               str(ROOT / 'sys/hal/gfx/gfx_policy.c'),
               str(ROOT / 'sys/hal/gfx/gfx_hw_mono_cpu.c'),
               str(ROOT / 'userland/lib/gfx/draw/gfx_cmd.c'),
               str(ROOT / 'userland/lib/gfx/draw/gfx_copy.c'), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), result
    if mutation:
        assert result.stdout.startswith('FAIL '), result
    print(('MUTATE RED ' + mutation[0]) if mutation else 'PASS A0 declarations/foundations (' + runner + ')')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        for mutation in (None, *MUTATIONS) if args.mutate else (None,):
            with tempfile.TemporaryDirectory(prefix='gfx-accel-hal-') as td:
                run_case(Path(td), runner, mutation)
        if args.mutate:
            with tempfile.TemporaryDirectory(prefix='gfx-accel-a0-restored-') as td:
                run_case(Path(td), runner)


def run(ctx) -> int:
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_gfx_accel_a0.py', *(['--mutate'] if ctx.mutate else [])]
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
