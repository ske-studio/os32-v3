"""V8: real cold formatter + real bounded state-name lookup, ILP32."""
import subprocess
from ver_test_utils import ROOT, arguments, execute, function


def run(source, runner):
    names = function((ROOT/'kernel/module/module.c').read_text(), 'const char *kdebug_state_name(')
    try:
        return execute((ROOT/'tools/tests/ver_format_host.c').read_text(), runner,
                       {'ver_format_source.c': source, 'ver_state_source.c': names},
                       assembly='lib/kstring_asm.asm')
    except subprocess.CalledProcessError as error:
        print(error.stderr)
        raise


def main():
    args = arguments()
    source = (ROOT/'kernel/ver_format.c').read_text()
    assert run(source, args.runner).returncode == 0
    if args.mutate:
        old = 'if (s->boot.crc_valid) {'
        assert source.count(old) == 1
        result = run(source.replace(old, 'if (!s->boot.crc_valid) {'), args.runner)
        assert result.returncode == 1 and 'FAIL:' in result.stdout
        print('RED: crc_valid inverted')


if __name__ == '__main__':
    main()
