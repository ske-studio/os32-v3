"""Register tools/ppc/test_shell_putchar.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-shell-putchar',
    tier=1,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_shell_putchar.py',
     'platform/powermac/shell_putchar.c',
     'include/console.h',
     'include/types.h',
     'kernel/console/console_stream.h',
     'platform/powermac/lan_boot.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§4'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_shell_putchar.py', ['gcc', 'qemu-i386'])
