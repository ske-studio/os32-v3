CHECK = dict(
    name='exec-gui-run-host', tier=1,
    covers=['exec/exec.c', 'tools/tests/test_exec_gui_run.py',
            'tools/tests/host32.py'],
    needs=[], budget=2.8)


def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = ['python3', '-B', 'tools/tests/test_exec_gui_run.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env)
