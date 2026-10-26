"""V3: compile the public value ABI in C89 and execute all layout checks."""
import subprocess
from ver_test_utils import ROOT, arguments, execute


def main():
    args = arguments()
    fixture = (ROOT/'tools/tests/ver_layout_host.c').read_text()
    header = (ROOT/'sdk/include/os32/os32_ver_info.h').read_text()
    assert execute(fixture, args.runner, {'os32_ver_info.h': header}, 'c89').returncode == 0
    if args.mutate:
        old = '#define OS32_VER_TEXT 64'
        assert header.count(old) == 1
        try:
            execute(fixture, args.runner, {'os32_ver_info.h': header.replace(old, '#define OS32_VER_TEXT 63')}, 'c89')
        except subprocess.CalledProcessError as error:
            assert 'os32_ver_line_size_68' in error.stderr and 'negative' in error.stderr
            print('RED (compile-time ABI assertion): text=63')
        else:
            raise AssertionError('layout mutant survived')


if __name__ == '__main__':
    main()
