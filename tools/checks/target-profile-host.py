#!/usr/bin/env python3
CHECK = dict(name='target-profile-host', tier=1,
    covers=['arch/x86/kernel/kentry.asm', 'boot/loader_fat_new.asm', 'build/os32.ld', 'fs/fd_redirect.c', 'fs/pipe_buffer.c', 'mm/shm.c', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/mkshlib.py', 'tools/tests/kernel_includes.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'sdk/target_profile.py', 'sdk/link_guard.py', 'sdk/os32x_hdr.py',
            'sdk/os32_generations.py', 'tools/mkmod.py', 'tools/module_schema.py',
            'tools/gen_memmap.py', 'tools/tests/test_target_profile.py'], needs=[], budget=3.3)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_target_profile.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
