CHECK = dict(
    name='x87-host', tier=1,
    covers=['lib/softfloat/**', 'lib/divdi3.c', 'arch/x86/kernel/x87_emu*', 'tools/tests/kernel_includes.py', 'build/config.mk', 'sys/include/cpu.h', 'sys/include/abi.h', 'sys/include/callconv.h', 'sys/arch/x86/arch_abi.h', 'sys/arch/x86/arch_cpu.h', 'sys/arch/x86/x86_flags.h', 'sys/arch/x86/kernel/x87.c', 'sys/arch/x86/kernel/x87.h', 'sys/arch/x86/kernel/x87_nm.h',
            'sys/include/io.h', 'sys/arch/x86/arch_io.h', 'sys/arch/x86/kernel/gdt.h',
            'sys/kern/mm/paging.h', 'sys/fs/fd_redirect.h', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h',
            'sys/arch/ppc/arch_exec_storage.h', 'sys/arch/ppc/arch_jmp_storage.h', 'sys/kern/exec/appslot.h', 'sys/kern/exec/exec.c',
            'sys/kern/exec/exec_launch.inc', 'sys/kern/exec/exec_schedule.inc', 'sys/kern/exec/exec_syscall.inc',
            'tools/tests/x87_host.c', 'tools/tests/test_x87.py', 'tools/tests/host32.py'],
    needs=[], budget=15,
    kind='characterization',
)


def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = ['python3', '-B', 'tools/tests/test_x87.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env)
