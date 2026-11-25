CHECK = dict(name='restructure-p0-7', tier=1, needs=[], budget=3,
             covers=['tools/restructure/quality_candidates.py',
                     'tools/restructure/test_quality_candidates.py', 'tools/checks/run.py'],
             basis=['docs/tasks/restructure/P0.md P0-7'])


def run(ctx):
    import subprocess
    import sys
    return subprocess.call([sys.executable, '-B', 'tools/restructure/test_quality_candidates.py']
                           + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
