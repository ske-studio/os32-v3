#!/usr/bin/env python3
CHECK = dict(name='memmap-host', tier=3,
    covers=['sdk/include/os32/os32_sndl_table.h', 'sdk/target_profile.py', 'tools/tests/paging_source.py', 'tools/tests/host32.py', 'userland/rust/libos32gui/src/shlib.rs', 'tools/mkshlib.py', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/stub.rs', 'sdk/link/shlib.ld', 'sdk/link/app.ld', 'sys/kern/exec/exec_heap.c', 'sys/kern/mm/kmalloc.c', 'boot/loader_fat_new.asm', 'build/os32.ld', 'docs/02_memory.md', 'sys/fs/fd_redirect.c', 'sys/fs/pipe_buffer.c', 'sys/arch/x86/kernel/kentry.asm', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shm.c', 'sdk/rust/os32api/src/gui/proto.rs', 'tools/gen_memmap.py', 'tools/tests/layout_boot_fixture.h', 'tools/tests/memmap_boot_host.c', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_memmap_boot.py', 'tools/tests/test_memmap_gen.py', 'boot/boot_defs.h', 'sdk/kapi.json', 'tools/gen_generation_manifest.py', 'tools/tests/test_owner_reclaim.py', 'tools/check_artifacts.py', 'tools/tests/test_layout_defs.py', 'tools/tests/test_vmkernel_layout.py', 'tools/mkvmkernel.py', 'build/**', 'sdk/link/app_sys.ld', 'sdk/os32_generations.py', 'tools/tests/mutpar.py', 'sys/arch/x86/kernel/cpu_flags.inc', 'tools/tests/kernel_includes.py', 'tools/gen_build_id.py'],
    needs=['make:all'], budget=111.3,
    basis=['[C4]', '[C5]', 'MEMMAP_V3'],
    env_limit='合成RAMで配置と初期化を確認するが、実RAM配置と実MMUの障害は再現しない。',
)


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
