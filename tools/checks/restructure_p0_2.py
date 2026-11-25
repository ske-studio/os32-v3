CHECK = dict(name='restructure-p0-2', tier=1, needs=[], budget=2,
             covers=['tools/restructure/build_refs.py', 'tools/restructure/test_build_refs.py'],
             basis=['docs/tasks/restructure/P0.md P0-2'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/restructure/test_build_refs.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
