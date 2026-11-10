"""DIRECT blocks public mapping while SHADOW preserves the lease paths."""
CHECK = dict(name='gfx-map-policy-host', tier=1,
    covers=['tools/tests/recover_gf_dev_stub.c', 'exec/surface_query.c', 'gfx/gfx_core_bridge.inc',
            'gfx/gfx_core_framebuffer.inc', 'gfx/gfx_policy.h',
            'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/test_gfx_kernel_fb.py',
            'tools/tests/paging_source.py', 'tools/tests/test_gfx_map_policy.py',
            'drivers/video/cirrus/backend_cirrus.c', 'drivers/video/cirrus/cirrus_identify.c',
            'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pegc.c',
            'exec/access_walk.c', 'exec/lease.c', 'exec/redir_access.c', 'gfx/gfx_core.c',
            'gfx/gfx_emergency.c', 'gfx/gfx_scroll.c', 'platform/pc98/gfx_scroll.c', 'platform/pc98/gfx_init.c', 'platform/pc98/gfx_selector.c', 'platform/pc98/gfx_surfaces.c', 'kernel/console/display_restore.h', 'gfx/gfx_vram.c', 'kernel/sys.c',
            'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'tools/tests/kernel_layout.py', 'tools/tests/mutpar.py'],
    needs=['make:all'], budget=2.0)

import argparse
from test_gfx_kernel_fb import run_case


def run_case_policy(runner, mutate=False):
    result = run_case(None, runner)
    assert result.returncode == 0, (result.stdout, result.stderr)
    print(result.stdout, end='')
    if mutate:
        for name, changes in [
            ('lease-entry-guards', [
                ('exec/surface_query.c',
                 '    if (gfx_policy_blocks_map(role)) return OS32_ERR_NOSYS;\n    int rc = surface_api_source(role, &source);\n    return rc ? rc : surface_lease(',
                 '    int rc = surface_api_source(role, &source);\n    return rc ? rc : surface_lease('),
                ('exec/surface_query.c',
                 '    if (gfx_policy_blocks_map(role)) return OS32_ERR_NOSYS;\n    int rc = surface_api_source(role, &source);\n    return rc ? rc : surface_lease_bundle(',
                 '    int rc = surface_api_source(role, &source);\n    return rc ? rc : surface_lease_bundle(')]),
            ('bridge-guard', [('gfx/gfx_core.c',
                '    if (gfx_policy_blocks_map(LEDGER_ROLE_CLIENT)) goto fail;', '')])]:
            result = run_case(None, runner, extra_changes=changes)
            assert result.returncode == 1 and 'FAIL ' in result.stdout, (name, result.stdout, result.stderr)
            print('RED (runtime): ' + name)


def run(ctx):
    for i, runner in enumerate(ctx.runners):
        run_case_policy(runner, ctx.mutate and i == 0)
    return 0 if ctx.runners else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=['native', 'qemu'], default='native')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    run_case_policy(args.runner, args.mutate)
