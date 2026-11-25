import subprocess

CHECK = dict(name='restructure-p0-6', tier=1, needs=[], budget=2,
             covers=['tools/restructure/hal_inventory.py', 'tools/restructure/test_hal_inventory.py',
                     'sys/hal/input/input_service.c'],
             basis=['docs/tasks/restructure/P0.md P0-6', 'docs/design/tools/check_layers.md §8'])


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/restructure/test_hal_inventory.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
