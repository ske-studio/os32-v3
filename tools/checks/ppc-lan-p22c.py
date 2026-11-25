"""Register tools/ppc/test_lan_p22c.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='lan-p22c',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p22c.py', 'sys/arch/ppc/ppc_trap.h', 'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P22c'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p22c.py', ['gcc', 'qemu-i386'])
