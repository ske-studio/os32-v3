"""P0-3 host fixtures and negative checks."""
import ast
from pathlib import Path
import sys
import tempfile

import check_inventory as inventory


def verify(module):
    def classify(*paths):
        return module.candidates(dict(path='tools/checks/example.py', covers=paths, env=[]))

    assert classify('sound/hal/example.c') == ('L2', 'tests/hal/')
    assert classify('fs/example.c', 'mm/example.c') == ('L3', 'tests/integration/')
    assert classify('tools/accept/example.py') == ('L4', 'tests/system/')
    assert classify('tools/checks/example.py') == ('', '')
    def observe(ref, actual):
        positions = {line: [i for i, value in enumerate(actual) if value == line]
                     for line in ref}
        hit = module.attributable(ref, actual, positions, module.kbd_blocks(actual))
        return hit[0] if hit else None

    numbered = ['MUTATION 1 RED opcode', 'MUTATION 2 RED appended case', 'MUTATIONS 2/2 RED']
    assert observe(numbered, numbered[:1] + ['RED unrelated'] + numbered[1:]) == 2
    assert observe(numbered, numbered[1:]) is None
    first = 'RED  first  (rc=1) -> FAIL: oracle'
    second = 'RED  second  (rc=1) -> FAIL: oracle'
    added = 'RED  new case  (rc=1) -> FAIL: oracle'
    end = 'kbdnav mutations: 1/1 killed'
    wrapper = [first, end, second, end]
    assert observe(wrapper, [added, first, 'kbdnav mutations: 2/2 killed', second, end]) == 3
    assert observe(wrapper, [first, end]) is None
    bench = ['PASS clock', 'RED runtime: lost-list', 'RED runtime: teardown', 'PASS deployment']
    assert observe(bench, ['RED unrelated', *bench]) == 2
    assert observe(bench, bench[:-1]) is None
    with tempfile.TemporaryDirectory() as temp:
        checks = [dict(name='guest', path='guest.py', covers=[], env=['qemu-ppc'])]
        assert module.candidates(checks[0]) == ('', '')
        assert module.environment_level(checks[0]) == 'L4'
        for name, rows in [('host-0', 'check-guest\t1\t0'),
                           ('sha-ppc-qemu-3', 'check-guest\t625.49\t0'),
                           ('sha-ppc-qemu-4', 'check-guest\t2\t1')]:
            folder = Path(temp) / 'run' / name
            folder.mkdir(parents=True)
            (folder / 'check_times.tsv').write_text(rows)
        assert module.measured_times([Path(temp)], checks) == {'guest': 625.49}
        guard = 'if args.mutate:\n'
        reports = "    print('RED first')\n    print('RED second')"
        assert module.mutation_branch(ast.parse(guard + reports))[0] == 2
        loop = "    for case in ['a', 'b']:\n    "
        assert module.mutation_branch(ast.parse(guard + loop + reports.splitlines()[0]))[0] == 2
        assert module.mutation_branch(ast.parse(guard + '    run_cases()'))[0] == ''
        path = Path(temp) / 'fixture.py'
        path.write_text('MUTANTS = [1, 2]\n')
        assert module.mutation_table(path) == 2
        path.write_text('MUTANTS = [*other]\n')
        assert module.mutation_table(path) is None


if __name__ == '__main__':
    verify(inventory)
    if '--mutate' in sys.argv:
        source = Path(inventory.__file__).read_text()
        for anchor, replacement in [("owners.add('hal')", "owners.add('sound')"),
                                    ("'qemu-ppc' in check['env']", 'False'),
                                    ("c['name']: c['env']", "c['name']: []"),
                                    ('total += size * inner', 'total += inner'),
                                    ('len(owners) > 1', 'len(owners) > 2'),
                                    ('return len(tables[0].elts)', 'return 0'),
                                    ('return len(indices), indices', 'return 1, indices'),
                                    ('sum(n for _, n in hits)', 'hits[-1][1]'),
                                    ('lines[start:start+len(section)]', 'lines[start:start+1]'),
                                    ("if 'L4' in methods:", "if environment_level(check) or 'L4' in methods:")]:
            assert source.count(anchor) == 1
            namespace = dict(__file__=inventory.__file__, __name__='mutant')
            exec(compile(source.replace(anchor, replacement), inventory.__file__, 'exec'), namespace)
            import types
            try:
                verify(types.SimpleNamespace(**namespace))
            except AssertionError:
                print('RED', anchor)
            else:
                raise AssertionError(anchor)
    print('PASS restructure-p0-3')
