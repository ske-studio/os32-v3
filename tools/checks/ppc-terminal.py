"""Register tools/ppc/test_ppc_terminal.py with its design expectations."""
CHECK = dict(
    name='ppc-terminal',
    tier=1,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_ppc_terminal.py',
     'platform/powermac/lan_terminal.c',
     'platform/powermac/lan_wire.c',
     'platform/powermac/lan_boot.h',
     'include/types.h',
     'lib/kstring_c.c',
     'lib/kstring.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_DESIGN§3'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_ppc_terminal.py', ['gcc', 'qemu-i386'])
