"""Register tools/ppc/test_lan_p15.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-lan-p15',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p15.py',
     'sys/dev/net/gem/gem_ring.c',
     'tools/ppc/lan_p15_fake.c',
     'sys/dev/net/gem/gem.h',
     'sys/include/types.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P15'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p15.py', ['gcc', 'qemu-i386'])
