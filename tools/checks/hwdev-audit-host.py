#!/usr/bin/env python3
CHECK = dict(name='hwdev-audit-host', tier=1,
    covers=['tools/hwdev_audit.py', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/module_schema.py', 'kernel/module/kdebug_imports.def', 'docs/decisions/realhw/HWDEV_CORE_API.md', 'build/hwdev.ld', 'build/hwdev.mk', 'build/kdebug.mk', 'hwdev/**', 'build/checks.d/check-hwdev-audit-host.mk', 'tools/check_map.d/check-hwdev-audit-host.yaml', 'kdebug/gfinit*.c', 'kdebug/gfinit_decode/*.inc', 'kdebug/gfinit_op/*.inc', 'kdebug/gfinit/*.inc', 'kdebug/gfinit_plan_c/*.inc', 'kdebug/gfinit_pll/*.inc', 'kdebug/gfinit_resources/*.inc', 'kdebug/gfinit*.h', 'kdebug/recover*.h', 'kdebug/recover_gf.c', 'kdebug/generated/recover_gf_tables.inc', 'sdk/kapi.json', 'tools/tests/test_hwdev_audit.py', 'kdebug/gf_stage_state.c', 'kdebug/gf_stage_state.h', 'hwdev/gf/quiesce_rows.h'],
    needs=['make:all'], budget=3.626872)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_hwdev_audit.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/hwdev_audit.py', '--out', *shlex.split(ctx.make_vars['BUILD_OUT'])]
    with (ctx.root / (ctx.make_vars['BUILD_OUT'] + '/hwdev-audit.json')).open('w') as output:
        rc = subprocess.call(argv, cwd=ctx.root, env=env, stdout=output, close_fds=False)
    if rc:
        return rc
    return 0
