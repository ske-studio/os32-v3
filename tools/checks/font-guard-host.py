#!/usr/bin/env python3
CHECK = dict(name='font-guard-host', tier=1,
    covers=['tools/tests/font_guard_inactive.h', 'kernel/console/font_guard*', 'kernel/r1_fixture.h', 'arch/x86/kernel/isr_handlers.c', 'exec/exec_heap.c', 'exec/redir_access.c', 'kernel/irq/irq.c', 'kernel/irq/irq_math.c', 'mm/kmalloc.c', 'arch/x86/kernel/setjmp.asm', 'exec/exec.c', 'exec/appslot.h', 'include/**', 'arch/x86/**', 'tools/tests/host_arch/**', 'tools/tests/host32.py', 'tools/tests/test_exec_r1.py', 'tools/tests/font_guard_host.c', 'tools/tests/test_font_guard.py', 'arch/x86/kernel/cpu_flags.inc', 'tools/tests/kernel_includes.py'],
    needs=[], budget=1.520482)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_font_guard.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
