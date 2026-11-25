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


def environment_level(check):
    return 'L4' if 'qemu-ppc' in check['env'] else ''


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

    if 'L4' in methods:
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


def literal_size(expr):
    if isinstance(expr, (ast.List, ast.Tuple)) and not any(
            isinstance(item, ast.Starred) for item in expr.elts):
        return len(expr.elts)
    return None


def branch_count(body, sizes):
    """Count individual RED reports in straight-line cases and fixed loops."""
    total = 0
    sizes = sizes.copy()
    for stmt in body:
        if isinstance(stmt, ast.For):
            size = (sizes.get(stmt.iter.id) if isinstance(stmt.iter, ast.Name)
                    else literal_size(stmt.iter))
            inner = branch_count(stmt.body, sizes)
            if size is None or not inner or stmt.orelse:
                return None
            total += size * inner
        elif isinstance(stmt, ast.With):
            inner = branch_count(stmt.body, sizes)
            if inner is None:
                return None
            total += inner
        elif isinstance(stmt, ast.Assign):
            for target in stmt.targets:
                if isinstance(target, ast.Name):
                    sizes[target.id] = literal_size(stmt.value)
        elif isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Call):
            call = stmt.value
            if isinstance(call.func, ast.Name) and call.func.id == 'print' and call.args:
                arg = call.args[0]
                while isinstance(arg, ast.BinOp):
                    arg = arg.left
                if isinstance(arg, ast.JoinedStr) and arg.values:
                    arg = arg.values[0]
                if isinstance(arg, ast.Constant) and isinstance(arg.value, str):
                    if re.match(r'^(?:MUTATE )?RED\b', arg.value):
                        total += 1
            else:
                return None  # A helper may execute additional mutants.
        elif not isinstance(stmt, (ast.Assert, ast.Pass)):
            return None
    return total


def mutation_branch(tree):
    branches = [n for n in ast.walk(tree) if isinstance(n, ast.If) and (
        isinstance(n.test, ast.Attribute) and n.test.attr == 'mutate' or
        ast.unparse(n.test) == "'--mutate' in sys.argv")]
    if any(isinstance(n, ast.If) and isinstance(n.test, ast.Name)
           and n.test.id == 'mutate' for n in ast.walk(tree)):
        return '', 'mutation-flag-in-helper'
    branches = [branch for branch in branches if not (
        len(branch.body) == 1 and isinstance(branch.body[0], ast.Expr)
        and isinstance(branch.body[0].value, ast.Call)
        and isinstance(branch.body[0].value.func, ast.Attribute)
        and branch.body[0].value.func.attr == 'append'
        and [ast.unparse(arg) for arg in branch.body[0].value.args] == ["'--mutate'"])]
    if not branches:
        return '', 'no-simple-mutate-branch'
    if len(branches) != 1:
        return '', 'multiple-mutate-branches'
    branch = branches[0]
    if any(branch in ast.walk(n) for n in ast.walk(tree)
           if isinstance(n, (ast.For, ast.While))):
        return '', 'dynamic-control-or-helper'
    # A literal local case table can be counted even when each case has
    # its own error handling. Require one dispatch over the unmodified table.
    cases = [stmt for stmt in branch.body if isinstance(stmt, ast.Assign)
             and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name)
             and stmt.targets[0].id.lower() in ('mutations', 'mutants')
             and literal_size(stmt.value) is not None]
    if len(cases) == 1:
        table = cases[0]
        name = table.targets[0].id
        loops = [stmt for stmt in branch.body if isinstance(stmt, ast.For)]
        if len(loops) == 1:
            loop = loops[0]
            seq = loop.iter
            if isinstance(seq, ast.Call) and isinstance(seq.func, ast.Name) and (
                    seq.func.id == 'enumerate' and seq.args):
                seq = seq.args[0]
            if (isinstance(seq, ast.Name) and seq.id == name
                    and branch.body.index(table) < branch.body.index(loop)
                    and all(stmt is table or stmt is loop or isinstance(stmt, ast.Assert)
                            or isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Call)
                            and isinstance(stmt.value.func, ast.Name) and stmt.value.func.id == 'print'
                            for stmt in branch.body)
                    and not any(isinstance(n, (ast.Break, ast.Continue))
                                for n in ast.walk(loop))):
                return literal_size(table.value), 'local-case-table'
    count = branch_count(branch.body, {})
    if count:
        return count, 'mutate-branch'
    return '', 'dynamic-control-or-helper'


def measured_times(artifacts, checks):
    paths = [p for d in artifacts for p in d.rglob('check_times.tsv')]
    return time_inventory.medians(time_inventory.load(paths),
                                  {c['name']: c['env'] for c in checks})


def mutations(check):
    path = ROOT / check['path']
    if check['path'].startswith('tools/checks/'):
        run = next((n for n in ast.parse(path.read_text()).body
                    if isinstance(n, ast.FunctionDef) and n.name == 'run'), None)
        if run is None:
            return '', '', 'no-run-entry'
        scripts = {n.value for n in ast.walk(run) if isinstance(n, ast.Constant)
                   and isinstance(n.value, str) and '/' in n.value
                   and n.value.endswith('.py')}
        if len(scripts) > 1 and all((ROOT / name).is_file() for name in scripts):
            counts = [mutation_table(ROOT / name) for name in scripts]
            if all(count is not None for count in counts):
                return sum(counts), ','.join(sorted(scripts)), 'declaration-tables'
        if len(scripts) != 1:
            return '', '', 'multiple-scripts' if scripts else 'indirect-run-entry'
        path = ROOT / scripts.pop()
    if path.is_file():
        count = mutation_table(path)
        if count is not None:
            return count, str(path.relative_to(ROOT)), 'declaration-table'
        count, reason = mutation_branch(ast.parse(path.read_text()))
        return count, str(path.relative_to(ROOT)), reason
    return '', '', 'missing-script'


def is_report(line):
    return bool(re.search(r'\bRED\b', line)) and not re.search(
        r'^MUTATIONS\b|\d+\s*/\s*\d+\s+(?:(?:runtime|mutants?)\s+)?RED\b', line, re.I)


def references(runs):
    result = {}
    for times, logs in runs:
        for _, successes in time_inventory.load([Path(times)]):
            for name in successes:
                path = Path(logs) / ('check-%s.log' % name)
                if path.is_file():
                    result[name] = path.read_text(errors='replace').splitlines()
    return result


def kbd_blocks(lines):
    """Buffered integration.py reports end with a checked killed/total marker."""
    blocks = []
    reports = []
    for pos, line in enumerate(lines):
        if line.startswith('RED  ') and ' -> FAIL: ' in line:
            reports.append(line)
            continue
        end = re.fullmatch(r'kbdnav mutations: (\d+)/(\d+) killed', line)
        if end and len(reports) == int(end[1]) == int(end[2]):
            blocks.append((pos, set(reports), len(reports)))
        reports = []
    return blocks


def attributable(reference, lines, positions, blocks):
    # Match complete child runs even when a wrapper launches multiple scripts.
    prior = kbd_blocks(reference)
    red = [line for line in reference if is_report(line)]
    if prior and sum(n for _, _, n in prior) == len(red):
        hits = []
        for _, labels, _ in prior:
            matches = [(pos, n) for pos, actual, n in blocks if labels <= actual]
            if len(matches) != 1:
                return None
            hits.extend(matches)
        if len({pos for pos, _ in hits}) == len(hits):
            return sum(n for _, n in hits), [pos for pos, _ in hits]
        return None

    if not red:
        return None
    # Numbered reports may interleave. Require each exact label, its ordinal,
    # and the matching terminator, in order in the same job.
    if reference[-1] == f'MUTATIONS {len(red)}/{len(red)} RED' and all(
            line.startswith(f'MUTATION {i} RED ') for i, line in enumerate(red, 1)):
        hits = [positions.get(line, []) for line in red]
        if all(len(hit) == 1 for hit in hits):
            indices = [hit[0] for hit in hits]
            if indices == sorted(indices) and lines[indices[-1]+1:indices[-1]+2] == reference[-1:]:
                return len(indices), indices

    # A complete named transcript, or its RED section between stable control
    # and completion messages. Ambiguous / changed transcripts stay unknown.
    first = reference.index(red[0])
    section = reference[max(0, first-1):]
    if not section or not section[0].strip():
        section = reference
    starts = [pos for pos in positions.get(section[0], [])
              if lines[pos:pos+len(section)] == section
              ]
    if len(starts) == 1:
        start = starts[0]
        return sum(is_report(line) for line in lines[start:start+len(section)]), [start]
    return None


def ci_counts(directory, artifacts, refs):
    from collections import defaultdict
    results = {}
    for jobs in sorted(directory.glob('jobs-*.tsv')):
        run = jobs.stem.split('-')[1]
        for row in jobs.read_text().splitlines():
            job_id, job = row.split('\t')
            shard = re.fullmatch(r'(checks|ppc-qemu) \((\d+)\)', job)
            if not shard:
                continue
            group, index = shard.groups()
            suffix = ('-ppc-qemu-' if group == 'ppc-qemu' else '-') + index
            paths = [p for root in artifacts for p in (root/run).glob('*' + suffix + '/check_times.tsv')]
            if len(paths) != 1:
                raise ValueError(f'{job}: expected one timing artifact, got {paths}')
            _, names = time_inventory.load(paths)[0]
            log = directory / f'{run}-{group}__{index}_.log'
            if group == 'ppc-qemu':
                # gate.render only publishes tails of failed checks.
                text = log.read_text(errors='replace')
                if '## 検査 シャード ' not in text or '(変異あり)' not in text:
                    raise ValueError(f'{log}: missing mutation gate report')
                for name in names:
                    results[name] = ('', log.name, 'gate-success-without-transcript')
                continue
            with log.open(errors='replace') as f:
                lines = [line.rstrip('\n').partition('Z ')[2] for line in f]
            relevant = {line for name in names for line in refs.get(name, [])}
            positions = defaultdict(list)
            for pos, line in enumerate(lines):
                if line in relevant:
                    positions[line].append(pos)
            blocks = kbd_blocks(lines)
            claims = {}
            owners = defaultdict(set)
            for name in names:
                ref = refs.get(name)
                hit = attributable(ref, lines, positions, blocks) if ref else None
                if hit:
                    claims[name] = hit
                    for pos in hit[1]:
                        owners[pos].add(name)
            for name in names:
                if name in claims and all(len(owners[p]) == 1 for p in claims[name][1]):
                    count, marks = claims[name]
                    results[name] = (count, f'{log.name}:{marks[0]+1}', 'ci-red')
                else:
                    reason = 'ambiguous-or-incomplete-transcript' if name in refs else 'no-reference'
                    results[name] = ('', log.name, reason)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('registry', type=Path)
    parser.add_argument('out', type=Path)
    parser.add_argument('artifacts', nargs='*', type=Path)
    parser.add_argument('--prior-run', action='append', nargs=2, default=[],
                        metavar=('TIMES_TSV', 'LOG_DIR'))
    parser.add_argument('--ci-logs', required=True, type=Path)
    args = parser.parse_args()
    observed = ci_counts(args.ci_logs, args.artifacts, references(args.prior_run))
    checks = json.loads(args.registry.read_text())
    times = measured_times(args.artifacts, checks)
    keys = ['name', 'path', 'tier', 'env', 'covers', 'basis', 'kind']
    with (args.out / 'source_disagreements.tsv').open('w', newline='') as diff, (
            args.out / 'checks.tsv').open('w', newline='') as full, (
            args.out / 'no_candidate.tsv').open('w', newline='') as missing:
        table, gaps = [csv.writer(f, delimiter='\t', lineterminator='\n') for f in (full, missing)]
        table.writerow(keys + ['mutation_count', 'mutation_source', 'mutation_basis', 'source_estimate', 'source_path', 'source_basis', 'disagreement', 'env_level', 'ci_seconds', 'level', 'destination'])
        differences = csv.writer(diff, delimiter='\t', lineterminator='\n')
        differences.writerow(['name', 'mutation_count', 'mutation_source', 'source_estimate', 'source_path'])
        gaps.writerow(['name', 'level', 'destination'])
        for check in sorted(checks, key=lambda c: c['name']):
            level, dest = candidates(check)
            values = [json.dumps(check[k], ensure_ascii=False) if isinstance(check[k], list)
                      else check[k] for k in keys]
            count, log, reason = observed.get(check['name'], ('', '', 'no-success-row'))
            estimate, path, basis = mutations(check)
            mismatch = int(count != estimate) if count != '' and estimate != '' else ''
            if mismatch == 1:
                differences.writerow([check['name'], count, log, estimate, path])
            env_level = environment_level(check)
            table.writerow(values + [count, log, reason, estimate, path, basis, mismatch,
                                     env_level, times.get(check['name'], ''), level, dest])
            if not level or not dest:
                gaps.writerow([check['name'], level, dest])


if __name__ == '__main__':
    main()
