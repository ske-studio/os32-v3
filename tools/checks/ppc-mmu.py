"""Register tools/ppc/mmu_run.py with its design expectations."""
CHECK = dict(env=['ppc-cross', 'qemu-ppc'],
    name='ppc-mmu',
    tier=2,
    budget=600,
    needs=['make:ppc-ci-image'],
    covers=['tools/ppc/mmu_run.py',
     'arch/ppc/**',
     'platform/powermac/**',
     'build/ppc.mk',
     'tools/ppc/**',
     'sys/include/types.h',
     'sys/kern/kstring.h',
     'sys/kern/kstring_c.c',
     'sys/arch/ppc/takeover.S',
     'sys/arch/ppc/pmap.c',
     'sys/arch/ppc/mmu.S',
     'sys/platform/powermac/mmu_boot.c',
     'sys/arch/ppc/cpu_setup.c',
     'sys/arch/ppc/cache_copy.S',
     'sys/arch/ppc/vectors.S',
     'sys/arch/ppc/exception.c',
     'sys/arch/ppc/decrementer.c',
     'sys/arch/ppc/entry.S',
     'sys/platform/powermac/boot.c',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_MMU_DESIGN§7'],
    env_limit='QEMU mac99 の MMU/DEC 模型であり実機固有の割込み・cache 障害は保証しない。',
    full_only=True,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/mmu_run.py', ['gcc', 'qemu-i386', 'qemu-system-ppc', 'genisoimage', '/usr/share/qemu/openbios-ppc'])
