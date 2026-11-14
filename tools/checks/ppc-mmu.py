"""Register tools/ppc/mmu_run.py with its design expectations."""
CHECK = dict(
    name='ppc-mmu',
    tier=2,
    budget=600,
    needs=['make:kernel'],
    covers=['tools/ppc/mmu_run.py',
     'arch/ppc/**',
     'platform/powermac/**',
     'build/ppc.mk',
     'tools/ppc/**',
     'include/types.h',
     'lib/kstring.h',
     'lib/kstring_c.c',
     'arch/ppc/takeover.S',
     'arch/ppc/pmap.c',
     'arch/ppc/mmu.S',
     'platform/powermac/mmu_boot.c',
     'arch/ppc/cpu_setup.c',
     'arch/ppc/cache_copy.S',
     'arch/ppc/vectors.S',
     'arch/ppc/exception.c',
     'arch/ppc/decrementer.c',
     'arch/ppc/entry.S',
     'platform/powermac/boot.c',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_MMU_DESIGN§7'],
    env_limit='QEMU mac99 の MMU/DEC 模型であり実機固有の割込み・cache 障害は保証しない。',
    full_only=True,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/mmu_run.py', ['gcc', 'qemu-i386', 'qemu-system-ppc', 'genisoimage', '/usr/share/qemu/openbios-ppc'])
