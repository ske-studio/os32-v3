"""Prefix accounting, fixed C deadlines and real failure paths (ILP32/qemu)."""
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
    ('kdebug/gfinit_budget.inc', 'runner_bus_rows',
     'total += (unsigned long long)GF_INIT_MAINS * GF_INIT_SCHEDULE * GF_REC_NODE_ROWS;', 'total += 0;'),
    ('kdebug/recover_common/session.inc', 'c_deadline_switch',
     'r->budget=GF_C_EXEC_SECONDS * PIT_HZ; r->spent=0;', 'r->budget=1; r->spent=0;'),
    ('kdebug/recover_common/session.inc', 'c_tx_reset',
     'r->log_limit=GF_C_TX_SECONDS * PIT_HZ; r->tx_ticks=0;', 'r->tx_ticks=0;'),
    ('kdebug/recover_common/session.inc', 'c_poll_limit',
     'gf_stage_c_running(r) ? GF_C_EXEC_POLLS : r->profile->max_polls', 'r->profile->max_polls'),

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
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation, MUTATIONS,
                        ('kdebug/gfinit_budget.inc', 'control', '', ''))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
