"""Register tools/ppc/test_lan_p8.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-lan-p8',
    tier=1,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_p8.py',
     'tools/ppc/pci_fake_lane.h', 'platform/powermac/pci_config.c',
     'tools/ppc/test_lan_p8_fake.c',
     'platform/powermac/lan_boot.h',
     'arch/ppc/ppc_oea.h',
     'arch/ppc/kernel_boot.h',
     'arch/ppc/ppc_boot.h',
     'drivers/net/gem/gem.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_LAN_DESIGN.P8'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_p8.py', ['gcc', 'qemu-i386'])
