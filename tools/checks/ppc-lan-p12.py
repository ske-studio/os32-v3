"""Register tools/ppc/test_lan_p12.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-lan-p12',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p12.py',
     'platform/powermac/gmac_dma.c',
     'tools/ppc/lan_p12_fake.c',
     'platform/powermac/lan_boot.h',
     'platform/powermac/ppc_memmap.h',
     'drivers/net/gem/gem.h',
     'mm/paging.h',
     'include/types.h',
     'arch/ppc/ppc_oea.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P12'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p12.py', ['gcc', 'qemu-i386'])
