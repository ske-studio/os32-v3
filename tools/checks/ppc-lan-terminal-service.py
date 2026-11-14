"""Register tools/ppc/test_lan_terminal_service.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-lan-terminal-service',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_lan_terminal_service.py',
     'platform/powermac/lan_service.c',
     'platform/powermac/lan_terminal.c',
     'tools/ppc/lan_p24_fake.c',
     'platform/powermac/*.h',
     'drivers/net/gem/*.c',
     'drivers/net/gem/*.h',
     'platform/powermac/gmac_dma.c',
     'platform/powermac/lan_*.c',
     'mm/paging.h',
     'include/*.h',
     'arch/ppc/*.h',
     'lib/kstring.h',
     'sdk/include/os32/*.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_DESIGN§3'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_lan_terminal_service.py', ['gcc', 'qemu-i386'])
