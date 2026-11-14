"""Register tools/ppc/test_cli_boot.py with its design expectations."""
CHECK = dict(
    name='ppc-cli-boot',
    tier=1,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_cli_boot.py',
     'platform/powermac/cli_boot.c',
     'platform/powermac/cli_boot.h',
     'kernel/cli/resident_cli.c',
     'kernel/cli/resident_cli.h',
     'platform/powermac/lan_boot.h',
     'kernel/console/console_stream.h',
     'fs/vfs.h',
     'include/types.h',
     'exec/exec.h',
     'lib/kstring_c.c',
     'lib/kstring.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_DESIGN§7'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_cli_boot.py', ['gcc', 'qemu-i386'])
