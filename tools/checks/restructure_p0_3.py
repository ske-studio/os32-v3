CHECK = dict(name='restructure-p0-3', tier=1, needs=[], budget=3,
             covers=['tools/restructure/check_inventory.py', 'tools/restructure/test_inventory.py',
                     'tools/ci/time_inventory.py', 'tools/checks/weights.json'],
             basis=['docs/tasks/restructure/P0.md P0-3'])


def run(ctx):
    import subprocess
    import sys
    return subprocess.call([sys.executable, '-B', 'tools/restructure/test_inventory.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
