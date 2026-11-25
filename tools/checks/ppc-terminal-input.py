"""Register tools/ppc/test_ppc_terminal_input.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-terminal-input',
    tier=1,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_ppc_terminal_input.py',
     'sys/platform/powermac/terminal_fifo.inc', 'sys/platform/powermac/terminal_input.c',
     'sys/platform/powermac/platform_hwb_session.h', 'sys/platform/powermac/usb_boot.h', 'sys/kern/idle_work.h',
     'sys/platform/powermac/console_stream.c',
     'sys/platform/powermac/lan_terminal.c',
     'sys/platform/powermac/lan_boot.h',
     'sys/kern/cli/resident_cli.h',
     'sys/include/types.h',
     'sys/kern/kstring_c.c',
     'sys/kern/kstring.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_DESIGN§3/§4'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_ppc_terminal_input.py', ['gcc', 'qemu-i386'])
