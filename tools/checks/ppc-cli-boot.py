"""Register tools/ppc/test_cli_boot.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-cli-boot',
    tier=1,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_cli_boot.py',
     'sys/platform/powermac/cli_boot.c',
     'sys/platform/powermac/cli_boot.h',
     'sys/kern/cli/resident_cli.c',
     'sys/kern/cli/resident_cli.h',
     'sys/platform/powermac/lan_boot.h',
     'sys/kern/console/console_stream.h',
     'sys/fs/vfs.h',
     'sys/include/types.h',
     'sys/kern/exec/exec.h',
     'sys/kern/kstring_c.c',
     'sys/kern/kstring.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_DESIGN§7'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_cli_boot.py', ['gcc', 'qemu-i386'])
