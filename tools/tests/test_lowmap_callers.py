"""Low PT transaction: genuine paging callers, tokens, restore and TLB order."""
CHECK = dict(name='lowmap-callers-host', tier=1,
    covers=['platform/pc98/bios_snapshot.h', 'platform/pc98/boot_capture.c', 'tools/tests/test_v86_low.py', 'arch/x86/**', 'arch/x86/kernel/idt.h', 'arch/x86/kernel/kentry.asm', 'arch/x86/legacy/lowmap.c', 'arch/x86/legacy/v86_call.c', 'boot/loader_fat_new.asm', 'build/os32.ld', 'docs/02_memory.md', 'drivers/block/loop_dev.h', 'drivers/input/kbd.c', 'drivers/video/pc98/backend_pegc.c', 'exec/exec.c', 'exec/exec_*.inc', 'fs/fd_redirect.c', 'fs/pipe_buffer.c', 'fs/vfs.h', 'gfx/gfx.h', 'gfx/gfx_core.c', 'include/**', 'include/legacy_guest.h', 'include/legacy_lowmap.h', 'include/platform_emergency.h', 'include/platform_memory.h', 'include/platform_timer.h', 'init/kernel.c', 'kernel/kselftest.h', 'kernel/module/module.h', 'kernel/time/sysclk.c', 'arch/x86/legacy/v86.c', 'arch/x86/legacy/v86.h', 'platform/pc98/legacy/v86_bios.*', 'platform/pc98/legacy/v86_bios_*.inc', 'platform/pc98/legacy/v86_io.h', 'arch/x86/legacy/v86_mem.*', 'arch/x86/legacy/v86_module.h', 'platform/pc98/bios_snapshot.c', 'lib/kstring.h', 'mm/memory_boot.c', 'mm/paging.*', 'mm/paging_lowmap.inc', 'mm/pgalloc.c', 'mm/pgalloc.h', 'mm/physmem.c', 'mm/shm.c', 'platform/pc98/**', 'platform/pc98/clock.c', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sdk/include/os32/**', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/gen_memmap.py', 'tools/mkshlib.py', 'tools/tests/exec_source.py', 'tools/tests/font_guard_inactive.h', 'tools/tests/host32.py', 'tools/tests/host_arch/**', 'tools/tests/kernel_boot_source.py', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_layout.py', 'tools/tests/legacy_guest_fake.h', 'tools/tests/mutpar.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/paging_source.py', 'tools/tests/pgalloc_host_fixture.h', 'tools/tests/shm_user_host.c', 'tools/tests/test_lowmap_callers.py', 'tools/tests/test_owner_reclaim.py', 'tools/tests/test_shm_user.py', 'tools/tests/v86_low_host.c', 'userland/rust/libos32gui/src/shlib.rs'], needs=['make:kernel'], budget=15)
import test_v86_low as fixture
fixture.MUTATIONS = (
    ('mm/paging.c', '    page_directory[0] = s->master_pde;', '', 'master-PDE'),
    ('mm/paging.c', '    ((u32 *)P2V(s->root))[0] = s->active_pde;', '', 'active-PDE'),
    ('mm/paging.c', '    arch_mmu_flush_tlb();\n    for (u32 i = 0; i < s->page_count; i++)',
     '    lowmap_token = 0;\n    arch_mmu_flush_tlb();\n    for (u32 i = 0; i < s->page_count; i++)', 'early-unlock'),
    ('mm/paging.c', '        page_tables[0][i] = saved;',
     '        if (i != 1) page_tables[0][i] = saved;', 'remaining-alias'),
    ('mm/paging.c', '    if (lowmap_token && lowmap_token != s) return -1;',
     '    if (lowmap_token && lowmap_token != s) return 0;', 'foreign-restore'),
    ('mm/paging.c', 'lowmap_token != s || base >= end', 'base >= end', 'foreign-token'),
)
fixture.EXPECTED.update({
    'master-PDE': 'both PDEs restored',
    'active-PDE': 'both PDEs restored',
    'early-unlock': 'flush before token release',
    'remaining-alias': 'both PDEs restored',
    'foreign-token': 'foreign token rejected',
    'foreign-restore': 'foreign restore rejected',
})
if __name__ == '__main__':
    raise SystemExit(fixture.main())
def run(ctx):
    import os, subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_lowmap_callers.py',
                            *(['--mutate'] if ctx.mutate else [])],
                           cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
