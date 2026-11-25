CHECK = dict(name='restructure-tool-callers', tier=1, needs=[], budget=5,
             covers=['tools/restructure/tool_callers.py', 'tools/restructure/test_tool_callers.py'],
             basis=['docs/design/build/ownership.md A1 の不要の候補'])


def run(ctx):
    import subprocess
    args = ['python3', '-B', 'tools/restructure/test_tool_callers.py']
    return subprocess.call(args + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
