#!/usr/bin/env python3
CHECK = dict(name='kbd-inject-host', tier=1,
    covers=['tools/tests/con_sink_arch/arch_io.h', 'tools/tests/host_arch/arch_io.h', 'sys/include/ksetjmp.h', 'sys/arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'tools/tests/host32.py', 'sys/kern/kbd_inject.c', 'tools/tests/kbd_inject_host.c', 'tools/tests/test_kbd_inject.py', 'sys/kern/console/con_sink.c', 'sys/kern/exec/appslot.h', 'sys/include/kbd_inject.h', 'sys/include/con_sink.h', 'sys/include/launch.h', 'sys/kern/kdebug/selftest_kbd_inject.c', 'sys/include/kselftest_probe.h', 'sys/kern/kdebug/selftest_internal.h', 'tools/tests/kernel_includes.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=[], budget=3.0,
    basis=['KAPI_SPEC §console シンクと実 RAM 量 (v46)'],
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kbd_inject.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
