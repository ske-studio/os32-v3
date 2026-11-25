#!/usr/bin/env python3
CHECK = dict(name='gshell-host', tier=1,
    covers=['sys/kern/av_focus.c', 'sys/include/av_contract.h', 'sys/kern/exec/exec_gui.h', 'sys/kern/exec/exec_hooks.h', 'sys/hal/input/in2_int.h', 'sys/include/app_ids.h', 'sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'userland/gshell/**', 'sdk/rust/os32api/**', 'sdk/rust/os32api/src/ls.rs', 'tools/tests/os32api_host.py', 'sys/kern/exec/shell_service.h', 'userland/lib/font/**', 'sdk/include/os32/os32_kapi_shared.h', 'tools/fetch_fonts.py', 'tools/tests/g16_fixture.py', 'tools/gen_g16.py', 'tools/g16/**', 'assets/ipadic/*.csv', 'tools/tests/test_stop_culprit_wm.py'],
    needs=[], budget=12.5,
    kind='characterization',
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'userland/gshell/host/integration.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    if ctx.mutate:
        return subprocess.call(['python3', '-B', 'tools/tests/test_stop_culprit_wm.py', '--mutate'],
                               cwd=ctx.root, env=env, close_fds=False)
    return 0
