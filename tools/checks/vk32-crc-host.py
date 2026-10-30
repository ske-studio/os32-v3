#!/usr/bin/env python3
CHECK = dict(name='vk32-crc-host', tier=1,
    covers=['lib/le_internal.h', 'sdk/link/app_sys.ld', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'boot/loader_fat_new.asm', 'boot/lz4_mini.c', 'boot/vk32_boot.c', 'tools/mkvmkernel.py', 'tools/tests/test_vk32_crc.py', 'tools/tests/test_vmkernel_layout.py', 'tools/tests/test_stack_entry.py', 'tools/tests/stack_entry_host.c', 'kernel/kstack_hw.c', 'kernel/kstack_hw.h', 'arch/x86/kernel/tss.c', 'arch/x86/kernel/tss.h', 'arch/x86/kernel/setjmp.asm', 'include/ksetjmp.h', 'init/kernel.c', 'arch/x86/kernel/isr_handlers.c', 'lib/sqlite3/sqlite_stack.asm', 'tools/gen_memmap.py', 'build/kernel.mk', 'build/os32.ld', 'include/memmap.h', 'tools/tests/vk32_old_lz4_3e22825.asm', 'tools/tests/vk32_host.c', 'lib/crc32_core.inc', 'tools/check_artifacts.py', 'boot/bootinfo.inc', 'fs/fd_redirect.c', 'fs/pipe_buffer.c', 'arch/x86/kernel/kentry.asm', 'mm/shm.c', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/mkshlib.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'build/out/kernel.bin', 'build/out/*', 'kernel/sqlite_engine.c', 'arch/x86/kernel/cpu_flags.inc', 'boot/vk32_defs.inc', 'boot/lz4_defs.inc', 'boot/crc32_defs.inc', 'boot/fat12_defs.inc', 'boot/pc98_defs.inc', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc'],
    needs=['make:all'], budget=14.716896)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_vk32_crc.py', '--real', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
