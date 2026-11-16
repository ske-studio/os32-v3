CHECK = dict(
    name='x87-host', tier=1,
    covers=['lib/softfloat/**', 'lib/divdi3.c', 'arch/x86/kernel/x87_emu*', 'tools/tests/kernel_includes.py', 'build/config.mk', 'include/cpu.h', 'include/abi.h', 'include/callconv.h', 'arch/x86/arch_abi.h', 'arch/x86/arch_cpu.h', 'arch/x86/x86_flags.h', 'arch/x86/kernel/x87.c', 'arch/x86/kernel/x87.h', 'arch/x86/kernel/x87_nm.h',
            'include/io.h', 'arch/x86/arch_io.h', 'arch/x86/kernel/gdt.h',
            'mm/paging.h', 'fs/fd_redirect.h', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h',
            'arch/ppc/arch_exec_storage.h', 'arch/ppc/arch_jmp_storage.h', 'exec/appslot.h', 'exec/exec.c',
            'exec/exec_launch.inc', 'exec/exec_schedule.inc', 'exec/exec_syscall.inc',
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
