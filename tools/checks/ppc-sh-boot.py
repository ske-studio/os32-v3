"""Register the PowerMac shell boot host test."""
CHECK = dict(name='ppc-sh-boot', tier=1, budget=60, needs=[],
             covers=['platform/powermac/input_usb.h', 'tools/tests/host32.py', 'platform/powermac/shell_boot.c', 'platform/powermac/shell_boot.h',
                     'platform/powermac/exec_boot.c', 'platform/powermac/lan_terminal.c',
                     'arch/ppc/exec_context.c', 'arch/ppc/arch_exec_storage.h',
                     'tools/tests/ppc_sh_boot.py'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/ppc_sh_boot.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
