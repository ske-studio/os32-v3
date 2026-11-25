CHECK = dict(name='restructure-p1-0', tier=1, needs=[], budget=10,
             covers=['tools/restructure/p1_verify.py', 'tools/restructure/test_p1_verify.py',
                     'docs/tasks/restructure/data/scope.tsv'],
             basis=['docs/tasks/restructure/P1.md P1-0 担当の表'])


def run(ctx):
    import subprocess
    status = subprocess.call(['python3', '-B', 'tools/restructure/p1_verify.py'], cwd=ctx.root)
    if status:
        return status
    args = ['python3', '-B', 'tools/restructure/test_p1_verify.py']
    return subprocess.call(args + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
