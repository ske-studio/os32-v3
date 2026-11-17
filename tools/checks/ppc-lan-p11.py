"""Register tools/ppc/test_lan_p11.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-lan-p11',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p11.py',
     'arch/ppc/dma_cache.c',
     'tools/ppc/lan_p11_fake.c',
     'drivers/net/gem/gem.h',
     'include/types.h',
     'include/callconv.h',
     'arch/ppc/ppc_oea.h',
     'arch/ppc/ppc_boot.h',
     'build/targets/ppc-powermac.mk',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P11'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p11.py', ['gcc', 'qemu-i386'])
