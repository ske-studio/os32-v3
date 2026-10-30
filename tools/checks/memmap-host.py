#!/usr/bin/env python3
CHECK = dict(name='memmap-host', tier=3,
    covers=['tools/tests/host32.py', 'userland/rust/libos32gui/src/shlib.rs', 'tools/mkshlib.py', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/stub.rs', 'sdk/link/shlib.ld', 'sdk/link/app.ld', 'exec/exec_heap.c', 'mm/kmalloc.c', 'boot/bootinfo.inc', 'boot/loader_fat_new.asm', 'build/os32.ld', 'docs/02_memory.md', 'fs/fd_redirect.c', 'fs/pipe_buffer.c', 'arch/x86/kernel/kentry.asm', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shm.c', 'sdk/rust/os32api/src/gui/proto.rs', 'tools/gen_memmap.py', 'tools/tests/layout_boot_fixture.h', 'tools/tests/memmap_boot_host.c', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_memmap_boot.py', 'tools/tests/test_memmap_gen.py', 'boot/boot_defs.h', 'sdk/kapi.json', 'tools/gen_generation_manifest.py', 'tools/tests/test_owner_reclaim.py', 'tools/check_map.d/check-memmap-host.yaml', 'build/checks.d/check-memmap-host.mk', 'tools/check_artifacts.py', 'tools/tests/test_layout_defs.py', 'tools/tests/test_vmkernel_layout.py', 'tools/mkvmkernel.py', 'build/**', 'sdk/link/app_sys.ld', 'sdk/os32_generations.py', 'tools/tests/mutpar.py', 'arch/x86/kernel/cpu_flags.inc', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=142.943198)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_layout_defs.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_memmap_gen.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_memmap_boot.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
