"""V8: real cold formatter + real bounded state-name lookup, ILP32."""
import subprocess
from ver_test_utils import ROOT, arguments, execute, function


def pc98_format_source():
    """Compile the production formatting callbacks without collector hardware I/O."""
    backend = (ROOT/'platform/pc98/ver_backend.c').read_text()
    collect = function(backend, 'static void pc98_ver_collect(')
    return backend.replace(collect, '').replace('.collect = pc98_ver_collect,', '.collect = 0,')


def run(source, runner):
    names = function((ROOT/'kdebug/kdebug_module.c').read_text(), 'const char *kdebug_state_name(')
    try:
        return execute((ROOT/'tools/tests/ver_format_host.c').read_text(), runner,
                       {'ver_format_source.c': source, 'ver_state_source.c': names},
                       assembly='lib/kstring_asm.asm')
    except subprocess.CalledProcessError as error:
        print(error.stderr)
        raise


def main():
    args = arguments()
    source = pc98_format_source() + (ROOT/'kernel/ver_format.c').read_text()
    for define in ('', '#define OS32_X87_EMU_LINKED\n'):
        assert run(define + source, args.runner).returncode == 0
    if args.mutate:
        call = '    if (backend && backend->devices) backend->devices(s, out);'
        after = '    if (backend && backend->debug)'
        for old, new in [('; active', '; inactive'), ('Hz input=', 'Hz '), ('backend && backend->head', 'backend->head')]:
            assert source.count(old) == 1
            result = run(source.replace(old, new), args.runner)
            assert result.returncode != 0
            print('RED:', old)
        assert source.count(call) == source.count(after) == 1
        result = run(source.replace(call, '').replace(after, call + '\n' + after), args.runner)
        assert result.returncode == 1 and 'FAIL:' in result.stdout
        print('RED: devices after identity')
        old = 'if (s->boot.crc_valid) {'
        assert source.count(old) == 1
        result = run(source.replace(old, 'if (!s->boot.crc_valid) {'), args.runner)
        assert result.returncode == 1 and 'FAIL:' in result.stdout
        print('RED: crc_valid inverted')
        old = '#ifdef OS32_X87_EMU_LINKED'
        assert source.count(old) == 1
        for define in ('', '#define OS32_X87_EMU_LINKED\n'):
            result = run(define + source.replace(old, '#ifndef OS32_X87_EMU_LINKED'), args.runner)
            assert result.returncode == 1 and 'FAIL:' in result.stdout
        print('RED: emulator link flag inverted (both images)')


if __name__ == '__main__':
    main()
