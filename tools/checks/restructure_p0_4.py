CHECK = dict(name='restructure-p0-4', tier=1, needs=[], budget=2,
             covers=['tools/restructure/ci_baseline.py', 'tools/restructure/test_ci_baseline.py'],
             basis=['docs/tasks/restructure/P0.md P0-4', 'docs/POLICY_CI.md R1'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/restructure/test_ci_baseline.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
