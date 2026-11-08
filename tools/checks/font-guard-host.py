#!/usr/bin/env python3
CHECK = dict(name='font-guard-host', tier=1,
    covers=['tools/tests/syscall_source.py', 'include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'platform/pc98/irq_policy.c', 'platform/pc98/platform_irq_limits.h', 'gfx/gfx_emergency.c', 'gfx/gfx_emergency.h', 'kdebug/selftest_kbd_inject.c', 'kernel/kbd_inject.c', 'tools/tests/kbd_inject_host.c', 'tools/tests/test_kbd_inject.py', 'tools/tests/font_guard_inactive.h', 'kernel/console/font_guard*', 'kernel/r1_fixture.h', 'arch/x86/kernel/isr_handlers.c', 'exec/exec_heap.c', 'exec/redir_access.c', 'kernel/irq/irq.c', 'kernel/irq/irq_math.c', 'mm/kmalloc.c', 'arch/x86/kernel/setjmp.asm', 'exec/exec.c', 'exec/appslot.h', 'include/**', 'arch/x86/**', 'tools/tests/host_arch/**', 'tools/tests/host32.py', 'tools/tests/test_exec_r1.py', 'tools/tests/font_guard_host.c', 'tools/tests/test_font_guard.py', 'arch/x86/kernel/cpu_flags.inc', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'include/kapi_call.h', 'include/kapi_call_generated.h', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h'],
    needs=[], budget=2.9)


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
