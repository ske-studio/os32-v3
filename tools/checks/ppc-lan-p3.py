"""Register tools/ppc/test_lan_p3.py with its design expectations."""
CHECK = dict(
    name='ppc-lan-p3',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p3.py',
     'platform/powermac/gmac_tree.c',
     'tools/ppc/lan_p3_fake.c',
     'platform/powermac/lan_boot.h',
     'platform/powermac/ofw.h',
     'arch/ppc/ppc_boot.h',
     'include/types.h',
     'lib/kstring.h',
     'lib/kstring_c.c',
     'tools/tests/host32.py',
     'platform/powermac/pci_config.c',
     'arch/ppc/ppc_oea.h',
     'arch/ppc/kernel_boot.h',
     'drivers/net/gem/gem.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P3'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p3.py', ['gcc', 'qemu-i386'])
