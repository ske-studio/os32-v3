"""Register tools/ppc/test_lan_p22d.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='lan-p22d',
    tier=2,
    budget=120,
    needs=[],
    covers=['tools/ppc/test_lan_p22d.py',
     'arch/ppc/kernel_trap.c',
     'tools/ppc/test_lan_p22d*',
     'arch/ppc/pd_walk.c',
     'tools/ppc/test_lan_p22d_host.c',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P22d'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=True,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p22d.py', ['gcc', 'qemu-i386'])
