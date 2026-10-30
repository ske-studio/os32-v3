#!/usr/bin/env python3
CHECK = dict(name='memmap', tier=0,
    covers=['sdk/link/app_sys.ld', 'userland/rust/libos32gui/src/shlib.rs', 'tools/mkshlib.py', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/stub.rs', 'sdk/link/shlib.ld', 'sdk/link/app.ld', 'docs/02_memory.md', 'boot/bootinfo.inc', 'boot/loader_fat_new.asm', 'build/os32.ld', 'fs/fd_redirect.c', 'fs/pipe_buffer.c', 'arch/x86/kernel/kentry.asm', 'mm/shm.c', 'sdk/rust/os32api/src/gui/proto.rs', 'tools/gen_memmap.py', 'tools/tests/test_memmap_gen.py', 'boot/boot_defs.h', 'sdk/kapi.json', 'tools/gen_generation_manifest.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'tools/check_artifacts.py', 'tools/tests/kernel_includes.py', 'tools/gen_build_id.py', 'tools/tests/mutpar.py'],
    needs=['make:all'], budget=6.013576)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/gen_memmap.py', '--check']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', 'tools/gen_memmap.py', '--manifest', '--check']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
