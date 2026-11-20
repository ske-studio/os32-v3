"""Check the PowerMac kernel linker and fixed memory layout."""
import subprocess

CHECK = dict(name='ppc-kboot-layout', tier=2, budget=60, needs=[], env=['ppc-cross'],
             covers=['tools/checks/ppc-kboot-layout.py', 'tools/ppc/kboot_layout.py',
                     'arch/ppc/kernel.ld', 'platform/powermac/platform_kboot_layout.h',
                     'include/memmap.h', 'tools/gen_memmap.py', 'lib/kstring_c.c',
                     'lib/kstring.h', 'lib/crc32.c', 'lib/crc32_core.inc', 'lib/crc32.h',
                     'include/types.h', 'tools/ppc/kboot_root_image.py', 'sdk/target_profile.py',
                     # Inputs discovered transitively through gen_memmap.py.
                     'arch/x86/kernel/kentry.asm', 'boot/loader_fat_new.asm', 'build/os32.ld',
                     'fs/fd_redirect.c', 'fs/pipe_buffer.c', 'mm/shm.c', 'sdk/kapi.json',
                     'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld',
                     'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs',
                     'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py',
                     'tools/mkshlib.py', 'tools/tests/kernel_includes.py',
                     'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py',
                     'userland/rust/libos32gui/src/shlib.rs'],
             basis=['docs/decisions/ppc/PPC_KBOOT_DESIGN.md §4',
                    'docs/02_memory.md §2-1'])


def run(ctx):
    args = ['python3', '-B', 'tools/ppc/kboot_layout.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root)
