"""Register tools/ppc/test_lan_p14.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-lan-p14',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p14.py',
     'sys/dev/net/gem/gem_reset.c',
     'tools/ppc/lan_p14_fake.c',
     'sys/dev/net/gem/gem.h',
     'sys/include/types.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P14'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p14.py', ['gcc', 'qemu-i386'])
