#!/usr/bin/env python3
CHECK = dict(name='double-fault-host', tier=1,
    covers=['sys/include/ksetjmp.h', 'sys/arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'sys/hal/time/clock_source.h', 'sys/platform/pc98/clock_source.c', 'sys/platform/pc98/pit_time.c', 'sys/platform/pc98/pit_time.h', 'sys/arch/x86/kernel/double_fault.c', 'sys/arch/x86/kernel/double_fault.h', 'sys/arch/x86/kernel/gdt.c', 'sys/arch/x86/kernel/gdt.h', 'sys/arch/x86/kernel/tss.c', 'sys/arch/x86/kernel/tss.h', 'sys/arch/x86/kernel/idt.c', 'sys/arch/x86/kernel/idt.h', 'sys/platform/pc98/pit.h', 'sys/include/kernel_tick.h', 'sys/arch/x86/kernel/isr_stub.asm', 'sys/arch/x86/kernel/ring3_entry.asm', 'sys/platform/pc98/kernel.c', 'sys/arch/x86/arch_cpu.h', 'sys/arch/x86/x86_flags.h', 'tools/tests/test_double_fault.py', 'tools/tests/double_fault_host.c', 'tools/tests/host32.py', 'sys/include/memmap.h', 'sys/include/types.h', 'sys/platform/pc98/pc98.h', 'sys/include/text_screen.h', 'sys/dev/char/serial.h', 'sys/kern/mm/paging.h', 'sys/kern/sqlite_engine.h', 'sys/kern/sqlite_engine_body.h', 'sys/platform/pc98/pit_math.h', 'sys/platform/pc98/sysclk.h', 'sys/kern/kstring.h', 'tools/mkmod.py', 'tools/module_schema.py', 'sys/platform/pc98/legacy/v86_entry.asm', 'sys/platform/pc98/legacy/v86.h', 'sys/kern/exec/appslot.h', 'sys/include/cpu.h', 'sys/include/platform_emergency.h', 'sys/platform/pc98/emergency.c', 'tools/tests/kernel_includes.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=3.4,
    kind='characterization',
)


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
