CHECK = dict(name='ppc-rh-fb', tier=2, budget=600, needs=['make:all'], covers=[
    'platform/powermac/rh_observe.[ch]', 'arch/ppc/**',
    'init/ppc_kernel.c', 'platform/powermac/boot.c',
    'platform/powermac/mmu_boot.c', 'platform/powermac/mmu_start.c',
    'platform/powermac/ppc_layout.h', 'platform/powermac/lan_boot.h',
    'build/ppc.mk', 'build/ppc-kboot.mk', 'tools/ppc/rh_fb.py',
    'tools/ppc/kboot_boot.py', 'tools/ppc/kboot_boot_guest.c',
    'tools/checks/ppc-rh-fb.py', 'tools/ppc/kboot_vectors.py', 'tools/ppc/run.py',
    'tools/tests/kernel_includes.py',
    'lib/kstring_c.c',
    'mm/memory_boot.c', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c',
    'platform/powermac/escc.c', 'platform/powermac/escc_tree.c',
    'platform/powermac/gmac_tree.c', 'platform/powermac/kboot_capture.c',
    'platform/powermac/kboot_reserve.c', 'platform/powermac/memory.c',
    'platform/powermac/mpic_tree.c', 'platform/powermac/ofw.c',
    'tools/ppc/kboot_boot_host.c', 'tools/ppc/kboot_vectors_fixture.[cS]',
    'build/out/ppc/kboot-vectors/**'])


def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/ppc/rh_fb.py'] +
                           (['--mutate'] if ctx.mutate else []),
                           cwd=ctx.root, env=dict(os.environ))
