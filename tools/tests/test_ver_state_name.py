"""V4: run the actual kdebug name function, including all 17 states."""
from ver_test_utils import ROOT, arguments, execute, function

NAMES = ['OFF', 'RUNNING', 'NOT_FOUND', 'BAD_HEADER', 'PAIR_MISMATCH',
         'NO_IMAGE_CRC', 'ABI_MISMATCH', 'NO_PAGES', 'SHORT_READ', 'BAD_CRC',
         'INIT_FAILED', 'RELOC_INVALID', 'IMPORT_INVALID', 'RECORD_FULL',
         'BAD_DEPENDENCY', 'BUSY', 'QUARANTINED']


def run(source, runner):
    checks = '\n'.join(f'CHECK(equal(kdebug_state_name({i}), "{name}"));' for i, name in enumerate(NAMES))
    fixture = '#include "support.h"\n' + source + '\nvoid _start(void) {\n' + checks + '''
    CHECK(equal(kdebug_state_name(-1), "UNKNOWN"));
    CHECK(equal(kdebug_state_name(17), "UNKNOWN"));
    CHECK(equal(kdebug_state_name(0x7fffffff), "UNKNOWN"));
    CHECK(equal(kdebug_state_name(-2147483647 - 1), "UNKNOWN"));
    say("PASS kdebug names: 0..16 and negative/upper/huge bounds\\n"); die(0);
}\n'''
    return execute(fixture, runner)


def main():
    args = arguments()
    source = function((ROOT/'kernel/module/module.c').read_text(), 'const char *kdebug_state_name(')
    assert run(source, args.runner).returncode == 0
    if args.mutate:
        old = 'sizeof(names) / sizeof(names[0])'
        assert source.count(old) == 1
        result = run(source.replace(old, '13'), args.runner)
        assert result.returncode == 1 and 'FAIL:' in result.stdout
        print('RED: state upper bound=13')


if __name__ == '__main__':
    main()
