"""Register tools/ppc/test_lan_p24d.py with its design expectations."""
CHECK = dict(
    name='ppc-lan-p24d',
    tier=1,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p24d.py',
     'platform/powermac/lan_diag.c',
     'tools/ppc/lan_p24d_test.c',
     'platform/powermac/lan_boot.h',
     'drivers/net/gem/gem.h',
     'platform/powermac/kboot_info.h',
     'arch/ppc/ppc_pd.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_SERVICE_DESIGN§2'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p24d.py', ['gcc', 'qemu-i386'])
