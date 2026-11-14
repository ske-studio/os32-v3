"""Register tools/ppc/test_lan_p20.py with its design expectations."""
CHECK = dict(
    name='ppc-lan-p20',
    tier=1,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p20.py',
     'platform/powermac/lan_log.c',
     'tools/ppc/lan_p20_test.c',
     'platform/powermac/lan_boot.h',
     'platform/powermac/kboot_log.h',
     'arch/ppc/ppc_oea.h',
     'include/types.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P20'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p20.py', ['gcc', 'qemu-i386'])
