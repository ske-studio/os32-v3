"""Inventory the sys/ trial move. Run from the repository root, passing an output directory."""
import argparse
import ast
from collections import Counter, defaultdict
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import move_files

ROOTS = set('init kernel mm exec arch platform drivers hwdev kdebug gfx sound input fs net kapi include lib'.split())
TOKEN = re.compile(r'[A-Za-z0-9_./*?+\-]+')
SOURCE = {'.c', '.asm', '.S', '.s', '.rs', '.cpp'}


def table(path, header, rows):
    with path.open('w', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
        writer.writerow(header.split())
        writer.writerows(rows)


def category(name):
    if name.startswith('.github/workflows/'):
        return 'workflow'
    if '/skills/' in name:
        return 'skill'
    if name.endswith('.md'):
        return 'document'
    if re.search(r'check_[^/]*_allow\.txt$', name):
        return 'allow'
    if 'baseline' in name and name.endswith('.json'):
        return 'baseline'
    return 'tools' if name.startswith('tools/') else 'source/build'


def operand(node):
    return node.value if isinstance(node, ast.Constant) and isinstance(node.value, str) else ast.unparse(node)


def annotations(name, data):
    tags, sites = {}, []
    if name.endswith('.py'):
        try:
            tree = ast.parse(data)
        except SyntaxError:
            return tags, sites
        for node in ast.walk(tree):
            if isinstance(node, ast.keyword) and node.arg == 'covers':
                for child in ast.walk(node.value):
                    if isinstance(child, ast.Constant) and isinstance(child.value, str):
                        tags[child.lineno, child.value] = 'covers'
            if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                    and node.func.attr == 'replace' and len(node.args) >= 2
                    and ('--mutate' in data or 'MUTATE' in data)):
                sites.append((node.lineno, operand(node.args[0]), 'replace operand; mutation role 未確定'))
            if ((isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Div, ast.Add))) or
                    isinstance(node, ast.JoinedStr) or (isinstance(node, ast.Call) and
                    isinstance(node.func, ast.Attribute) and node.func.attr in ('joinpath', 'join'))):
                fragments = [c.value for c in ast.walk(node) if isinstance(c, ast.Constant)
                             and isinstance(c.value, str)]
                if any(piece.strip('/').split('/')[0] in ROOTS for piece in fragments):
                    sites.append((node.lineno, ast.unparse(node), 'constructed path; evaluation 未確定'))
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and
                    t.id.upper() in ('MUTANTS', 'MUTATIONS') for t in node.targets):
                for item in getattr(node.value, 'elts', [node.value]):
                    sites.append((item.lineno, ast.unparse(item), 'mutation declaration; target 未確定'))
            if isinstance(node, ast.FunctionDef) and 'mutat' in node.name.lower():
                for line in range(node.lineno, node.end_lineno + 1):
                    tags[line, None] = 'mutation context'
    return tags, sites


def dry_run(index_entry, mapping, temp):
    env = dict(os.environ, GIT_INDEX_FILE=str(Path(temp, 'index')))
    subprocess.run(['git', 'update-index', '-z', '--index-info'], input=index_entry + b'\0',
                   env=env, check=True)
    return json.loads(subprocess.check_output(
        [sys.executable, '-B', 'tools/move_files.py', '--dry-run', str(mapping)], env=env))


def scan(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    entries = {}
    for entry in subprocess.check_output(['git', 'ls-files', '-s', '-z']).split(b'\0'):
        if entry:
            _, name = entry.split(b'\t', 1)
            entries[name.decode()] = entry
    moves = {n: 'sys/' + n for n in entries if n.split('/')[0] in ROOTS and Path(n).is_file()}
    (out / 'map.json').write_text(json.dumps([{'from': n, 'to': v} for n, v in moves.items()], indent=2))
    tracked = {n for n in entries if Path(n).suffix in SOURCE | {'.h', '.inc', '.py', '.mk', '.ld'}}
    tracked_roots = {n.split('/')[0] for n in tracked}
    parents = defaultdict(list)
    by_base = defaultdict(set)
    aliases = {}
    for name in moves:
        parents[str(Path(name).parent)].append(name)
        for alias in move_files.path_replacements({name: moves[name]}):
            by_base[Path(alias).name].add(name)
            aliases[alias] = name
    leaf_pattern = re.compile(r'(?<![A-Za-z0-9_.+\-])(?:'
                              + '|'.join(re.escape(base) for base in sorted(by_base, key=len, reverse=True))
                              + r')(?![A-Za-z0-9_.+\-])')

    def holder(name):
        if name.startswith('docs/archive/') or not Path(name).is_file():
            return [], [], None
        raw = Path(name).read_bytes()
        if b'\0' in raw:
            return [], [], None
        try:
            data = raw.decode()
        except UnicodeError:
            return [], [], None
        rows, residual = [], []
        selected = {name} if name in moves else set()
        for leaf in set(leaf_pattern.findall(data)):
            selected.update(by_base[leaf])
        tagged, sites = annotations(name, data)
        for line, expr, basis in sites:
            kind = 'constructed path' if basis.startswith('constructed') else 'mutation operand'
            rows.append((kind, name, line, json.dumps(expr, ensure_ascii=False), basis))
        group = category(name)
        for number, line in enumerate(data.splitlines(), 1):
            tokens = TOKEN.findall(line)
            quoted = set(re.findall(r"""['"]([\w.+-]+)['"]""", line))
            for token in tokens:
                value = token.rstrip('.')
                local = os.path.normpath(Path(name).parent / value)
                root_parts = value.split('/')
                matched = next(('/'.join(root_parts[i:]) for i, part in enumerate(root_parts)
                                if part in ROOTS and i + 1 < len(root_parts)), None)
                declared = tagged.get((number, value))
                known = matched in aliases or local in aliases or value in tracked or local in tracked
                if (matched or known or declared or value == '__FILE__' or
                        (value in ROOTS and value in quoted) or
                        ('/' in value and value.split('/')[0] in tracked_roots and '*' in value)):
                    kind = declared or tagged.get((number, None), group)
                    basis = 'tracked source/object' if known else 'literal token; target 未確定'
                    if value == '__FILE__':
                        kind, basis = '__FILE__ source', 'lexical macro occurrence'
                    if declared and not known:
                        basis = 'covers AST literal; expansion 未確定'
                    rows.append((kind, name, number, value, basis))
            for inc in re.findall(r'-I([\w/]+)', line):
                if inc in parents:
                    selected.add(parents[inc][0])
                    rows.append((group, name, number, '-I' + inc, 'mapped include directory'))
        # Each invocation uses the real CLI and this holder's real index entry.
        # Irrelevant mappings cannot match a complete basename, split literal or -I parent.
        report = {}
        if selected and not name.startswith(move_files.EXCLUDED):
            with tempfile.TemporaryDirectory() as temp:
                mapping = Path(temp, 'moves.json')
                mapping.write_text(json.dumps([{'from': n, 'to': moves[n]} for n in sorted(selected)]))
                report = dry_run(entries[name], mapping, temp)
                report['holder'] = name
                for key in ('unresolved_paths', 'split_paths'):
                    for item in report[key]:
                        residual.append((name, item['line'], item['path'], key))
        # Rewrites preserve line count. Check each token on its resulting line,
        # including lines where one exact filename changed alongside a glob.
        changed = {}
        for edit in report.get('replacements', []):
            for diff_line in edit['diff'][2:]:
                if diff_line.startswith('@@'):
                    lineno = int(re.match(r'@@ -[0-9,]+ \+(\d+)', diff_line)[1])
                elif diff_line.startswith('+'):
                    changed[lineno] = diff_line[1:]
                    lineno += 1
                elif diff_line.startswith(' '):
                    lineno += 1
        for kind, holder_name, num, token, basis in rows:
            if kind == 'constructed path':
                if num not in changed:
                    residual.append((name, num, token, 'constructed expression unchanged; evaluation 未確定'))
                continue
            if basis == 'mapped include directory':
                if '-Isys/' + token[2:] not in changed.get(num, ''):
                    residual.append((name, num, token, 'include directory not supplemented by dry-run'))
                continue
            if kind == 'mutation operand':
                continue
            if ('*' in token or '?' in token) and token.split('/')[0] in ROOTS and (num not in changed or token in changed[num]):
                residual.append((name, num, token, 'glob outside literal map'))
            elif name.startswith(move_files.EXCLUDED) and token.split('/')[0] in ROOTS:
                residual.append((name, num, token, 'excluded holder'))
            elif (token.strip('/') in parents or token in ROOTS) and (num not in changed or token in changed[num]):
                residual.append((name, num, token, 'directory literal; use 未確定'))
        return rows, residual, report

    rows, residual = [], []
    witness = None
    with (out / 'dry-run.jsonl').open('w') as log, ThreadPoolExecutor(args.jobs) as pool:
        for refs, warnings, report in pool.map(holder, entries):
            rows.extend(refs)
            residual.extend(warnings)
            if report:
                log.write(json.dumps(report) + '\n')
                name = report['holder']
                if report['replacements'] and (witness is None or
                        Path(name).stat().st_size < Path(witness['holder']).stat().st_size):
                    witness = report
    if witness:
        name = witness['holder']
        with tempfile.TemporaryDirectory() as temp:
            full = dry_run(entries[name], out / 'map.json', temp)
        full['holder'] = name
        (out / 'dry-run-full.json').write_text(json.dumps(full, indent=2))
        for key in ('replacements', 'unresolved_paths', 'split_paths', 'regenerate'):
            if full[key] != witness[key]:
                raise ValueError('full map and projected map disagree: ' + name + ' ' + key)
    if args.check_index:
        effective = []
        for check in json.loads(args.check_index.read_text())['checks']:
            if check['path'] not in entries:
                continue
            for spelling in check['covers']:
                effective.append((check['name'], check['path'], spelling, 'registered covers'))
        table(out / 'effective_covers.tsv', 'check declaration spelling evidence', effective)
    table(out / 'refs.tsv', 'kind holder line token evidence', rows)
    table(out / 'path_refs.tsv', 'kind evidence occurrences', [(*key, n) for key, n in
          sorted(Counter((row[0], row[-1]) for row in rows).items())])
    table(out / 'unresolved.tsv', 'holder line spelling evidence', sorted(set(residual)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--check-index', type=Path, help='check registry generated by make all')
    parser.add_argument('--jobs', type=int, default=6)
    scan(parser.parse_args())
