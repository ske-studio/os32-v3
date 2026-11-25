"""Register tools/ppc/test_lan_p16.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-lan-p16',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p16.py',
     'sys/dev/net/gem/gem_mac.c',
     'sys/dev/net/gem/gem.h',
     'sys/include/types.h',
     'sys/include/callconv.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P16'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p16.py', ['gcc', 'qemu-i386'])
