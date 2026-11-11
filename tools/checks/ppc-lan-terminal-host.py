import subprocess

CHECK = dict(name='ppc-lan-terminal-host', tier=1, budget=60, needs=[],
             covers=['tools/ppc_lan_host.py', 'tools/ppc/test_lan_terminal_host.py'])


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/ppc/test_lan_terminal_host.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
