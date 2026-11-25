#!/usr/bin/env python3
CHECK = dict(name='font-guard-host', tier=1,
    covers=['tools/tests/syscall_source.py', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/platform/pc98/irq_policy.c', 'sys/platform/pc98/platform_irq_limits.h', 'sys/hal/gfx/gfx_emergency.c', 'sys/hal/gfx/gfx_emergency.h', 'sys/kern/kdebug/selftest_kbd_inject.c', 'sys/kern/kbd_inject.c', 'tools/tests/kbd_inject_host.c', 'tools/tests/test_kbd_inject.py', 'tools/tests/font_guard_inactive.h', 'kernel/console/font_guard*', 'sys/kern/r1_fixture.h', 'sys/arch/x86/kernel/isr_handlers.c', 'sys/kern/exec/exec_heap.c', 'sys/kern/exec/redir_access.c', 'sys/kern/irq/irq.c', 'sys/kern/irq/irq_math.c', 'sys/kern/mm/kmalloc.c', 'sys/arch/x86/kernel/setjmp.asm', 'sys/kern/exec/exec.c', 'sys/kern/exec/appslot.h', 'include/**', 'arch/x86/**', 'tools/tests/host_arch/**', 'tools/tests/host32.py', 'tools/tests/test_exec_r1.py', 'sys/hal/input/legacy_ring.c', 'sys/hal/input/legacy_mode.c', 'tools/tests/font_guard_host.c', 'tools/tests/test_font_guard.py', 'sys/arch/x86/kernel/cpu_flags.inc', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/include/kapi_call.h', 'sys/include/kapi_call_generated.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=[], budget=2.9,
    kind='characterization',
)


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
