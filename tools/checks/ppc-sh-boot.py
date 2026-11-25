"""Register the PowerMac shell boot host test."""
CHECK = dict(name='ppc-sh-boot', tier=1, budget=60, needs=[],
             covers=['sys/platform/powermac/input_usb.h', 'tools/tests/host32.py', 'sys/platform/powermac/shell_boot.c', 'sys/platform/powermac/shell_boot.h',
                     'sys/platform/powermac/exec_boot.c', 'sys/platform/powermac/lan_terminal.c',
                     'sys/arch/ppc/exec_context.c', 'sys/arch/ppc/arch_exec_storage.h',
                     'tools/tests/ppc_sh_boot.py'],
    basis=['PPC_SHELL_EXEC_DESIGN §2', 'PPC_SHELL_EXEC_DESIGN §3', 'PPC_SHELL_EXEC_DESIGN §4', 'PPC_SHELL_EXEC_DESIGN §5'],
    env_limit='hostのexec結果台本でshell起動分岐を確認するが、実USER遷移と実機起動障害は再現しない。',
)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/ppc_sh_boot.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
