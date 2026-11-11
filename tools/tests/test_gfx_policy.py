"""H4: policy transitions, saved hooks, revoke order and nonzero generations."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

CHECK = dict(name='gfx-policy-host', tier=1, needs=[], budget=15, covers=[
    'gfx/gfx_policy.c', 'gfx/gfx_policy.h', 'gfx/gfx_internal.h', 'gfx/gfx.h',
    'include/gfx_hal.h', 'include/con_sink.h', 'mm/pgalloc.h', 'exec/appslot.h',
    'tools/tests/gfx_policy_host.c', 'tools/tests/test_gfx_policy.py',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('bridge-rebind', '        v1_bridge_rebind();', '        (void)0;'),
    ('active-hooks', 'd && d->fill_rect', 'd && g_backend->fill_rect'),
    ('required-fill', ' && d->fill_rect', ''),
    ('fullscreen', 'con_sink_is_enabled() && !gfx_fullscreen_owner() &&', 'con_sink_is_enabled() && 1 &&'),
    ('gui', 'return con_sink_is_enabled() &&', 'return 1 &&'),
    ('owner', 'appslot_gfx_owner() != GFX_OWNER_WM', '0'),
    ('scan-clear', '    gfx_scan_changed = 0;', '    (void)gfx_scan_changed;'),
    ('scan-bump', '} else if (gfx_scan_changed) {', '} else if (0) {'),
    ('skip-zero', 'if (++gfx_policy_gen == 0) gfx_policy_gen = 1;', 'if (++gfx_policy_gen == 0) gfx_policy_gen = 0;'),
    ('revoke-order', 'gfx_reinit_surface_roles(0, 0);', 'gfx_reinit_surface_roles(1, 0);'),
    ('regen', 'gfx_reinit_surface_roles(1, 0);', '(void)0;'),
    ('roles', 'gfx_reinit_surface_roles(0, 0);', 'gfx_reinit_surface_roles(0, 1);'),
    ('early-policy', '        if (want == GFX_POLICY_DIRECT) {',
     '        g_backend->policy = want;\n        if (want == GFX_POLICY_DIRECT) {'),
    ('early-hooks', '            gfx_reinit_surface_roles(0, 0);',
     '            g_backend->fill_rect = g_backend->direct_ops->fill_rect;\n            gfx_reinit_surface_roles(0, 0);'),
    ('client-map', 'role == LEDGER_ROLE_CLIENT', '0'),
    ('display-map', 'role == LEDGER_ROLE_DISPLAY', '0'),
    ('map-shadow', 'g_backend->policy == GFX_POLICY_DIRECT &&', '1 &&'),
    ('required-blit', ' && d->blit', ''),
    ('required-fill_rop', ' && d->fill_rop', ''),
    ('required-put', ' && d->put', ''),
    ('required-get', ' && d->get', ''),
)


def run_case(folder, runner, mutation=None):
    edits = {}
    if mutation:
        name, old, new = mutation
        path = "gfx/gfx_policy.c"
        source = (ROOT / path).read_text()
        assert source.count(old) == 1, name
        edits[path] = source.replace(old, new)
    exe = folder / 'hal-host'
    command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
               '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
               '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
               *include_flags(ROOT),
               str(ROOT / 'tools/tests/gfx_policy_host.c'),
               str(ROOT / 'gfx/gfx_policy.c'), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), result
    if mutation:
        assert result.stdout.startswith('FAIL '), result
    print(('MUTATE RED ' + mutation[0]) if mutation else 'PASS H4 policy/transitions (' + runner + ')')


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
    argv = ['python3', '-B', 'tools/tests/test_gfx_policy.py', *(['--mutate'] if ctx.mutate else [])]
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
