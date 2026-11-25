CHECK = dict(
    name='x87-lifecycle-host', tier=1,
    covers=['sys/kern/av_focus.c', 'sys/kern/exec/ring3_str.c', 'sys/hal/input/in2_int.h', 'sys/include/av_contract.h', 'sys/include/ksetjmp.h', 'sys/arch/x86/arch_jmp_storage.h', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/kern/exec/exec.c', 'exec/exec_*.inc', 'sys/arch/x86/kernel/setjmp.asm',
            'sys/arch/x86/kernel/cpu_flags.inc', 'tools/tests/x87_lifecycle_host.c',
            'tools/tests/test_x87_lifecycle.py', 'tools/tests/exec_source.py',
            'tools/tests/host_arch/*.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=[], budget=4.1,
    kind='characterization',
)


def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = ['python3', '-B', 'tools/tests/test_x87_lifecycle.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env)
