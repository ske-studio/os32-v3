"""Register tools/ppc/test_lan_p2.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-lan-p2',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p2.py', 'tools/tests/fixtures/ppc_gemreg.h',
     'sys/dev/net/gem/gem.h',
     'sys/include/types.h',
     'sys/include/callconv.h',
     'build/targets/ppc-powermac.mk',
     'sys/platform/powermac/lan_boot.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P2'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p2.py', ['gcc', 'qemu-i386', str(ctx.root / 'tools/tests/fixtures/ppc_gemreg.h')])
