import subprocess

CHECK = dict(name='restructure-p2-1', tier=1, needs=[], budget=15,
             covers=['tools/build_compare.py', 'tools/ci/sep_identity.py',
                     'tools/gen_build_id.py', 'tools/restructure/test_manifest_identity.py'],
             basis=['docs/design/tools/ci/sep_identity.md §6'])


def run(ctx):
    args = ['python3', '-B', 'tools/restructure/test_manifest_identity.py']
    return subprocess.call(args + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
