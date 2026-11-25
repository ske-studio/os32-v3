"""P0-3: inventory a saved run.py --list --json and CI artifact directories."""
import argparse
import ast
import csv
import json
import re
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ci'))
import time_inventory


def candidates(check):
    owners = set()
    methods = set()
    for path in check['covers']:
        top, _, rest = path.partition('/')
        if path == check['path']:
            continue
        if path.startswith(('sound/hal/', 'input/hal/')) or (
                top == 'include' and rest.endswith('_contract.h')):
            owners.add('hal')
        elif top in ('kernel', 'mm', 'exec', 'init'):
            owners.add('kern')
        elif top in ('drivers', 'hwdev'):
            owners.add('dev')
        elif top in ('arch', 'platform', 'gfx', 'sound', 'input', 'fs', 'net',
                     'kapi', 'boot', 'lib', 'sdk', 'userland'):
            owners.add(top)
        elif path.startswith(('tools/ci/', 'tools/checks/')):
            methods.add('M')
        elif path.startswith('tools/accept/'):
            methods.add('L4')
        elif path.startswith('tools/check_'):
            methods.add('L0')
        elif top == 'tools' and not rest.startswith(('tests/', 'ppc/')):
            owners.add('tools')
        elif path.startswith('build/') or path == 'Makefile':
            methods.add('L1')

    if 'qemu-ppc' in check['env'] or 'L4' in methods:
        return 'L4', 'tests/system/'
    if owners:
        if len(owners) > 1:
            return 'L3', 'tests/integration/'
        owner = owners.pop()
        level = 'L0' if 'L0' in methods else 'L2'
        return level, 'tests/%s/' % owner
    if 'M' in methods:
        return 'M', 'tests/infra/'
    return ','.join(sorted(methods)), ''


def mutation_table(path):
    tree = ast.parse(path.read_text())
    tables = [node.value for node in tree.body if isinstance(node, ast.Assign)
              and any(isinstance(name, ast.Name) and name.id in ('MUTATIONS', 'MUTANTS')
                      for name in node.targets)]
    if len(tables) == 1 and isinstance(tables[0], (ast.Tuple, ast.List)):
        if not any(isinstance(item, ast.Starred) for item in tables[0].elts):
            return len(tables[0].elts)
    return None


def mutations(check):
    path = ROOT / check['path']
    if check['path'].startswith('tools/checks/'):
        run = next((n for n in ast.parse(path.read_text()).body
                    if isinstance(n, ast.FunctionDef) and n.name == 'run'), None)
        if run is None:
            return '', ''
        scripts = {n.value for n in ast.walk(run) if isinstance(n, ast.Constant)
                   and isinstance(n.value, str) and n.value.startswith('tools/')
                   and n.value.endswith('.py')}
        if len(scripts) != 1:
            return '', ''
        path = ROOT / scripts.pop()
    if path.is_file():
        count = mutation_table(path)
        if count is not None:
            return count, str(path.relative_to(ROOT))
    return '', ''


def red_lines(log):
    """Count individual RED reports, excluding totals, in a successful prior run."""
    return sum(bool(re.search(r'\bRED\b', line)) for line in log.splitlines()
               if not re.search(r'\d+\s*/\s*\d+|^MUTATIONS\b', line)
               and re.match(r'^\s*(?:RED\b|(?:MUTATION|MUTANT)\s+\S+\s+RED\b)', line, re.I))


def observations(runs):
    result = {}
    for times, logs in runs:
        for _, successes in time_inventory.load([Path(times)]):
            for name in successes:
                path = Path(logs) / ('check-%s.log' % name)
                if path.is_file():
                    result[name] = red_lines(path.read_text(errors='replace'))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('registry', type=Path)
    parser.add_argument('out', type=Path)
    parser.add_argument('artifacts', nargs='*', type=Path)
    parser.add_argument('--prior-run', action='append', nargs=2, default=[],
                        metavar=('TIMES_TSV', 'LOG_DIR'))
    args = parser.parse_args()
    observed = observations(args.prior_run)
    checks = json.loads(args.registry.read_text())
    times = time_inventory.medians(time_inventory.load(
        [p for d in args.artifacts for p in d.rglob('check_times.tsv')]),
        {c['name']: c['env'] for c in checks})
    keys = ['name', 'path', 'tier', 'env', 'covers', 'basis', 'kind']
    with (args.out / 'checks.tsv').open('w', newline='') as full, (
            args.out / 'no_candidate.tsv').open('w', newline='') as missing:
        table, gaps = [csv.writer(f, delimiter='\t', lineterminator='\n') for f in (full, missing)]
        table.writerow(keys + ['mutation_table_count', 'mutation_source', 'prior_individual_red_lines', 'ci_seconds', 'level', 'destination'])
        gaps.writerow(['name', 'level', 'destination'])
        for check in sorted(checks, key=lambda c: c['name']):
            level, dest = candidates(check)
            values = [json.dumps(check[k], ensure_ascii=False) if isinstance(check[k], list)
                      else check[k] for k in keys]
            table.writerow(values + [*mutations(check), observed.get(check['name'], ''), times.get(check['name'], ''), level, dest])
            if not level or not dest:
                gaps.writerow([check['name'], level, dest])


if __name__ == '__main__':
    main()
