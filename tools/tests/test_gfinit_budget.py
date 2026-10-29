"""4c2c3 REC/cleanup accounting and real failure paths (ILP32/qemu)."""
import pathlib
import re
import subprocess
import sys
import tempfile

import host32
import mutpar
import test_gfinit_plan_c

ROOT = pathlib.Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('kdebug/gfinit_budget.inc', 'omit_check_records',
     'total++; /* CHECK result, also on mismatch or bus failure */',
     'total += 0; /* missing CHECK result */'),
    ('kdebug/recover_cleanup.h', 'omit_local_targets',
     'pairs += t->index == RECOVER_CR1F ? 2 * n : n;',
     'pairs += t->index == RECOVER_CR1F ? 0 * n : 0 * n;'),
]


def command(exe):
    return [s.replace('gfinit_plan_c_host.c', 'gfinit_budget_host.c')
            for s in test_gfinit_plan_c.command(exe)]


def mutation(item):
    file, name, old, new = item
    source = (ROOT / file).read_text()
    if old and source.count(old) != 1:
        return name + ': ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gf-budget-mut-') as td:
        exe = pathlib.Path(td) / 'host'
        try:
            tree = mutpar.build_in_tree(ROOT, td, {file: source.replace(old, new, 1) if old else source},
                                       [command(exe)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name + ': compile failed ' + str(exc.stderr), 1
        result = host32.run([str(exe)], cwd=tree, timeout=120, capture_output=True)
        return name + (': RED' if result.returncode else ': GREEN'), int(not result.returncode)


def main():
    with tempfile.TemporaryDirectory(prefix='gf-budget-') as td:
        exe = pathlib.Path(td) / 'host'
        subprocess.run(command(exe), check=True, cwd=ROOT)
        result = host32.run([str(exe)], cwd=ROOT, timeout=120, capture_output=True)
        output = result.stdout.decode()
        print(output, end='')
        if result.stderr:
            print(result.stderr.decode(), file=sys.stderr)
        if result.returncode:
            return result.returncode
        rows = int(re.search(r'BUDGET case=15 c_rows=(\d+)', output)[1])
        monitor = (ROOT / 'tools/rshell_recover.py').read_text()
        limit = int(re.search(r'MODE_C_BYTES = (\d+)', monitor)[1])
        assert (rows + 40) * 385 <= limit, (rows, limit)
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation, MUTATIONS,
                        ('kdebug/gfinit_budget.inc', 'control', '', ''))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
