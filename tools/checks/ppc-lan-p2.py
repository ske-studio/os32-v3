"""Register tools/ppc/test_lan_p2.py with its design expectations."""
CHECK = dict(
    name='ppc-lan-p2',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p2.py',
     'drivers/net/gem/gem.h',
     'include/types.h',
     'include/callconv.h',
     'build/ppc-kboot.mk',
     'platform/powermac/lan_boot.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P2'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p2.py', ['gcc', 'qemu-i386', '/home/hight/os32-tmp/ref/openbsd/sys/dev/ic/gemreg.h'])
