"""Register tools/ppc/test_lan_p19.py with its design expectations."""
CHECK = dict(
    name='ppc-lan-p19',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p19.py',
     'drivers/net/gem/gem_rx.c',
     'tools/ppc/lan_p19_fake.c',
     'drivers/net/gem/gem.h',
     'drivers/net/gem/gem_ring.c',
     'include/types.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P19'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p19.py', ['gcc', 'qemu-i386'])
