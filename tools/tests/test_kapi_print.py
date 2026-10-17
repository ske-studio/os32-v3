"""T4b-8 kprintf: real B1/renderer with separate VA/PA (ILP32)."""
import argparse
import pathlib
import json
import re
import subprocess
import test_access_walk as walk

ROOT = walk.ROOT
TARGET_SRC = [*walk.FILES.values(), 'kapi/kapi_print.c', 'lib/kprintf.c',
              'lib/kprintf_attr.c', 'exec/exec.c']
MUTANTS = [
    ('format raw pointer', 'print',
     'format, sizeof(format))) return 0;',
     'format, sizeof(format))) return 0;\n    '
     'for (u32 i = 0; i < sizeof(format); i++) { format[i] = ((const char *)(uptr)fixed[1])[i]; if (!format[i]) break; }'),
    ('string raw pointer', 'print',
     'args[count] = (u32)(uptr)strings[count];',
     'args[count] = args[count];'),
    ('argument raw pointer', 'print',
     'count++;',
     'if (spec != \'s\') args[count] = ((const u32 *)(uptr)va)[2 + count];\n        count++;'),
    ('window overrun', 'print', 'count == KAPI_PRINT_ARG_LIMIT',
     'count > KAPI_PRINT_ARG_LIMIT'),
    ('partial output before string validation', 'print',
     'strings[count], KAPI_PRINT_STRING_CAP)) goto out;',
     'strings[count], KAPI_PRINT_STRING_CAP)) { '
     'kvprintf((u8)fixed[0], "early", args); goto out; }'),
    ('staging leak', 'print', 'if (strings[i]) kfree(strings[i]);',
     'if (strings[i]) (void)strings[i];'),
]


def run(runner, mutant=None):
    sources = {k: (ROOT / p).read_text() for k, p in walk.FILES.items()}
    setup = (ROOT / 'tools/tests/access_walk_host.c').read_text()
    setup = setup.replace('void __cdecl kprintf(', 'void __cdecl host_log(')
    setup = setup.replace('#include "pgalloc_host_fixture.h"',
                          '#include "' + str(ROOT / 'tools/tests/pgalloc_host_fixture.h') + '"')
    setup = setup.replace('    caller_copy_tests();', '    caller_copy_tests();\n    die(0);')
    sources['setup'] = setup
    for key, path in [('print', 'kapi/kapi_print.c'), ('format', 'lib/kprintf.c'),
                      ('attr', 'lib/kprintf_attr.c')]:
        sources[key] = (ROOT / path).read_text()
    dispatch = (ROOT / 'exec/exec.c').read_text()
    start = dispatch.index('    if (slot == KAPI_SLOT_KPRINTF) {')
    end = dispatch.index('    int strings = ', start)
    sources['dispatch'] = dispatch[start:end]
    if mutant:
        name, key, old, new = mutant
        assert sources[key].count(old) == 1, name
        sources[key] = sources[key].replace(old, new)
    result = walk.run(sources, fixture='kapi_print_host.c', runner=runner)
    if mutant:
        assert result.returncode == 1 and 'FAIL:' in result.stdout, (
            name, result.returncode, result.stdout, result.stderr)
        print('RED (runtime):', name, result.stdout.strip())
    else:
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        print(result.stdout.strip())


def inventory():
    apis = json.loads((ROOT / 'sdk/kapi.json').read_text())['api']
    inputs = {a['name'] for a in apis
              if any('const char *' in p for p in a['args'])}
    document = (ROOT / 'docs/tasks/v3/TASK_T4_T6_MODULES.md').read_text()
    section = document.split('### 1-3. X-1 公開 NUL 入力・可変引数の担当表\n')[1].split('\n## 2.')[0]
    listed = set(re.findall(r'`(\w+)`', section))
    assert not inputs - listed, sorted(inputs - listed)
    assert {a['name'] for a in apis if '...' in a['args']} == {'kprintf'}
    print(f'PASS public input inventory: {len(inputs)} char-pointer APIs; variadic=kprintf')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    try:
        inventory()
        run(args.runner)
        if args.mutate:
            for mutant in MUTANTS:
                run(args.runner, mutant)
            print(f'PASS {len(MUTANTS)}/{len(MUTANTS)} mutations runtime RED')
    except subprocess.CalledProcessError as error:
        print(error.stdout or '', error.stderr or '')
        raise
