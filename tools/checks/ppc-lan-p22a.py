"""Register tools/ppc/test_lan_p22a.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='lan-p22a',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p22a.py',
     'arch/ppc/kernel_probe.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P22a'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p22a.py', ['gcc', 'qemu-i386'])
