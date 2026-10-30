#!/usr/bin/env python3
CHECK = dict(name='recover-tables-host', tier=1,
    covers=['kdebug/recover_gf.c', 'kdebug/generated/recover_gf_tables.inc', 'kdebug/recover_gf_expand.h', 'kdebug/recover_gf.h', 'kdebug/recover.h', 'include/types.h', 'drivers/pci/pci_decode.h', 'include/memmap.h', 'kdebug/gfinit_plan.h', 'kdebug/gfinit_decode.h', 'kdebug/gfinit_wire.h', 'kdebug/gfinit_pll.h', 'kdebug/recover.c', 'kdebug/recover_common/*.inc', 'kdebug/recover_plan/*.inc', 'kdebug/recover_gf_stage/*.inc', 'kdebug/recover_wire.h', 'lib/crc32_core.inc', 'tools/tests/recover_tables_oracle.c', 'tools/tests/recover_tables_oracle.json', 'tools/tests/recover_tables_host.c', 'tools/tests/recover_tables_host.h', 'tools/tests/test_recover_tables.py', 'tools/tests/host32.py', 'build/checks.d/check-recover-tables-host.mk', 'tools/check_map.d/check-recover-tables-host.yaml', 'tools/gen_recover_tables.py', 'tools/tests/recover_tables_bounds.h', 'kdebug/recover_cleanup.h', 'kdebug/recover_diag.h', 'include/pci_dev_contract.h', 'tools/gfinit_mode_tables.py', 'hwdev/gfinit_mode.h', 'kdebug/recover_gf_c.h', 'hwdev/generated/gf_mode_rows.inc', 'kdebug/gf_stage_state.h', 'kdebug/gfinit_pll.h', 'hwdev/gf_load_state.c', 'hwdev/gf_load_state.h', 'hwdev/gf/quiesce_rows.h'],
    needs=[], budget=2.210428)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['mkdir', '-p', *shlex.split(ctx.make_vars['BUILD_OUT'])]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_recover_tables.py', '--extract', *shlex.split(ctx.make_vars['BUILD_OUT'] + '/recover_tables_oracle.json')]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/gen_recover_tables.py', '--check', '--input', *shlex.split(ctx.make_vars['BUILD_OUT'] + '/recover_tables_oracle.json')]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
