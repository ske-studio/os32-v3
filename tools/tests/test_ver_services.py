"""V5: actual saved-state getter and dependencies, without diagnostic stubs."""
import re
from ver_test_utils import ROOT, arguments, execute, function


def run(source, runner):
    getter = function(source, 'void services_ver_state(')
    # Only these bounded reads/copy calls may appear; forbidden dependencies
    # also remain unresolved in the link (no printing, allocation or I/O mocks).
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', getter))
    assert calls <= {'services_ver_state', 'sqlite_engine_stack_high_water',
                     'services_state_name', 'kstrncpy', 'sizeof'}
    dependencies = '#define SQLITE_STACK_MARK 0xa5\nstatic u32 sqlite_stack_peak;\n'
    dependencies += 'char *kstrncpy(char *, const char *, u32);\n'
    dependencies += function(source, 'u32 sqlite_engine_stack_high_water(')
    # Skip the earlier prototype when extracting the state-name definition.
    dependencies += function(source[source.index('/* Diagnostic only;'):], 'static const char *services_state_name(')
    dependencies += getter
    return execute((ROOT/'tools/tests/ver_services_host.c').read_text(), runner,
                   {'services_source.c': dependencies}, assembly='lib/kstring_asm.asm')


def main():
    args = arguments()
    source = (ROOT/'kernel/sqlite_engine.c').read_text()
    result = run(source, args.runner)
    assert result.returncode == 0
    assert result.stdout.count('PASS services:') == 1 and result.stdout.count('\n') == 1
    if args.mutate:
        old = 'out->public = (u32)services_public;'
        assert source.count(old) == 1
        result = run(source.replace(old, 'out->public = 1;'), args.runner)
        assert result.returncode == 1 and 'FAIL:' in result.stdout
        print('RED: public always 1')


if __name__ == '__main__':
    main()
