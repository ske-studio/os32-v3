"""DIRECT blocks public mapping while SHADOW preserves the lease paths."""
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
