"""Register tools/ppc/test_ppc_terminal_input.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-terminal-input',
    tier=1,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_ppc_terminal_input.py',
     'platform/powermac/terminal_fifo.inc', 'platform/powermac/terminal_input.c',
     'platform/powermac/platform_hwb_session.h', 'platform/powermac/usb_boot.h', 'kernel/idle_work.h',
     'platform/powermac/console_stream.c',
     'platform/powermac/lan_terminal.c',
     'platform/powermac/lan_boot.h',
     'kernel/cli/resident_cli.h',
     'include/types.h',
     'lib/kstring_c.c',
     'lib/kstring.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_DESIGN§3/§4'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_ppc_terminal_input.py', ['gcc', 'qemu-i386'])
