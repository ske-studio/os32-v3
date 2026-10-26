"""V3: compile the public value ABI in C89 and execute all layout checks."""
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
