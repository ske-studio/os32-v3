"""H2: ILP32 DIRECT validation, clipping, dispatch and partial completion."""
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
    'gfx/gfx_internal.h', 'gfx/gfx_policy.h', 'gfx/gfx_hw_mono_cpu.h',
    'exec/exec.h', 'gfx/palette.h', 'include/io.h',
    'include/gfx_mode.h', 'include/gfx_format.h', 'include/types.h',
    'include/pc98.h', 'include/memmap.h', 'exec/surface_limits.h',
    'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_kapi_slots.h',
    'tools/tests/gfx_accel_hal_host.c', 'tools/tests/test_gfx_accel_hal.py',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('bridge-submit-begin', 'gfx/gfx_hw_submit.c',
     '(void)v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_R | GFX2_CPU_W);', '(void)0;'),
    ('bridge-submit-end', 'gfx/gfx_hw_submit.c',
     'v1_bridge_end(GFX2_ROLE_DISPLAY, GFX2_CPU_R | GFX2_CPU_W, 0);', '(void)0;'),
    ('bridge-get-begin', 'gfx/gfx_hw_submit.c',
     '(void)v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_R);', '(void)0;'),
    ('bridge-get-end', 'gfx/gfx_hw_submit.c',
     'v1_bridge_end(GFX2_ROLE_DISPLAY, GFX2_CPU_R, 0);', '(void)0;'),
    ('batch-limit', 'sdk/include/os32/os32_kapi_shared.h',
     '#define GFX_CMD_MAX  64', '#define GFX_CMD_MAX  63'),
    ('signed-coordinates', 'sdk/include/os32/os32_kapi_shared.h',
     'i16 x, y, w, h, sx, sy;', 'u16 x, y, w, h, sx, sy;'),
    ('done-guard', 'gfx/gfx_hw_submit.c',
     '!done || (user && !ring3_user_range_writable((u32)(uptr)done, sizeof(u32)))',
     '!done'),
    ('done-zero', 'gfx/gfx_hw_submit.c', '*done = 0;', '*done = 19;'),
    ('submit-policy', 'gfx/gfx_hw_submit.c',
     'if (!g_backend || g_backend->policy != GFX_POLICY_DIRECT) return OS32_ERR_NOSYS;\n    if (gen != gfx_policy_gen) return OS32_ERR_STALE;\n    if (n >',
     'if (!g_backend || g_backend->policy != GFX_POLICY_DIRECT) return 0;\n    if (gen != gfx_policy_gen) return OS32_ERR_STALE;\n    if (n >'),
    ('submit-gen', 'gfx/gfx_hw_submit.c',
     'if (gen != gfx_policy_gen) return OS32_ERR_STALE;\n    if (n >',
     '(void)gen; if (0) return OS32_ERR_STALE;\n    if (n >'),
    ('command-read', 'gfx/gfx_hw_submit.c',
     '(user && !ring3_user_range_ok((u32)(uptr)cmds, n * sizeof(GFX_Cmd)))', '0'),
    ('partial-done', 'gfx/gfx_hw_submit.c', '*done = i + 1;', '*done = i;'),
    ('key-range', 'gfx/gfx_hw_submit.c', '(c.key < -1 || c.key > 255)', '0'),
    ('rop-range', 'gfx/gfx_hw_submit.c', 'c.rop > GFX_ROP_XOR', 'c.rop > 2'),
    ('pitch-range', 'gfx/gfx_hw_submit.c', 'pitch < row', 'pitch == 0'),
    ('extent-overflow', 'gfx/gfx_hw_submit.c',
     '(u32)(h - 1) > (~(u32)0 - row) / pitch', '0'),
    ('address-overflow', 'gfx/gfx_hw_submit.c',
     'if (bytes - 1 > ~(u32)0 - data) return 0;', 'if (0) return 0;'),
    ('copy-source-clip', 'gfx/gfx_hw_submit.c',
     'if (w > width - sx) w = width - sx;', 'if (0) w = width - sx;'),
    ('put-offset', 'gfx/gfx_hw_submit.c', 'c.data + offset', 'c.data + offset * 0'),
    ('mono-transparent', 'gfx/gfx_hw_submit.c',
     'if (c.key >= 0 && g_backend->mono)', 'if (g_backend->mono)'),
    ('get-write', 'gfx/gfx_hw_submit.c',
     'pitch, (u32)w, h, user, 1)', 'pitch, (u32)w, h, user, 0)'),
    ('data-read', 'gfx/gfx_hw_submit.c',
     'return !user || (write ?', 'return !user || !write || (write ?'),
    ('get-offset', 'gfx/gfx_hw_submit.c', 'dst + offset, pitch', 'dst + offset * 0, pitch'),
    ('get-nosys', 'gfx/gfx_hw_submit.c',
     'int ox = x, oy = y, rc;\n    u32 offset;\n    if (!g_backend || g_backend->policy != GFX_POLICY_DIRECT) return OS32_ERR_NOSYS;',
     'int ox = x, oy = y, rc;\n    u32 offset;\n    if (!g_backend || g_backend->policy != GFX_POLICY_DIRECT) return 0;'),
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
    print(('MUTATE RED ' + mutation[0]) if mutation else 'PASS H2 DIRECT contract (' + runner + ')')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        for mutation in (None, *MUTATIONS) if args.mutate else (None,):
            with tempfile.TemporaryDirectory(prefix='gfx-accel-hal-') as td:
                run_case(Path(td), runner, mutation)

        if args.mutate:
            with tempfile.TemporaryDirectory(prefix='gfx-hal-restored-') as td:
                run_case(Path(td), runner)


def run(ctx) -> int:
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_gfx_accel_hal.py', *(['--mutate'] if ctx.mutate else [])]
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
