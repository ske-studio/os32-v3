"""V3: compile the public value ABI in C89 and execute all layout checks."""
CHECK = dict(name='ver-layout-host', tier=1,
    covers=['sdk/include/os32/os32_ver_info.h', 'tools/tests/ver_layout_host.c', 'include/types.h', 'tools/tests/ver_test_utils.py', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=60.0)

from ver_test_utils import ROOT, arguments, execute


def main():
    args = arguments()
    fixture = (ROOT/'tools/tests/ver_layout_host.c').read_text()
    header = (ROOT/'sdk/include/os32/os32_ver_info.h').read_text()
    assert execute(fixture, args.runner, {'os32_ver_info.h': header}, 'c89').returncode == 0
    if args.mutate:
        old = '#define OS32_VER_TEXT 64'
        assert header.count(old) == 1
        result = execute(fixture, args.runner,
                         {'os32_ver_info.h': header.replace(old, '#define OS32_VER_TEXT 63')}, 'c89')
        # Tail padding preserves both sizeof values. The constant CHECK is
        # therefore the expected detector, not a compile-time size failure.
        assert result.returncode == 1, result.returncode
        assert result.stdout == ('FAIL: OS32_VER_LINES == 20 && '
                                 'OS32_VER_TEXT == 64 && OS32_VER_COLS == 52\n'), result.stdout
        print('RED (runtime constant CHECK): text=63')


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_ver_layout.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
