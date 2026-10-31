#!/usr/bin/env python3
CHECK = dict(name='double-fault-host', tier=1,
    covers=['arch/x86/kernel/double_fault.c', 'arch/x86/kernel/double_fault.h', 'arch/x86/kernel/gdt.c', 'arch/x86/kernel/gdt.h', 'arch/x86/kernel/tss.c', 'arch/x86/kernel/tss.h', 'arch/x86/kernel/idt.c', 'arch/x86/kernel/idt.h', 'arch/x86/kernel/isr_stub.asm', 'arch/x86/kernel/ring3_entry.asm', 'init/kernel.c', 'arch/x86/arch_cpu.h', 'arch/x86/x86_flags.h', 'tools/tests/test_double_fault.py', 'tools/tests/double_fault_host.c', 'tools/tests/host32.py', 'include/memmap.h', 'include/types.h', 'include/pc98.h', 'include/tvram.h', 'drivers/char/serial.h', 'mm/paging.h', 'kernel/sqlite_engine.h', 'kernel/time/pit_math.h', 'kernel/time/sysclk.h', 'lib/kstring.h', 'tools/mkmod.py', 'tools/module_schema.py', 'kernel/v86/v86_entry.asm', 'kernel/v86/v86.h', 'exec/appslot.h', 'include/cpu.h', 'include/platform_emergency.h', 'platform/pc98/emergency.c', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=1.64824)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_double_fault.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
