"""Register tools/ppc/test_lan_p22b.py with its design expectations."""
CHECK = dict(
    name='lan-p22b',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p22b.py',
     'arch/ppc/kernel_probe.S',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P22b'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p22b.py', ['gcc', 'qemu-i386'])
