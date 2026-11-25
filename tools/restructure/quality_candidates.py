"""P0-7: emit review leads and their source evidence from a registration tree."""
import argparse
import ast
from collections import defaultdict
import csv
from fnmatch import fnmatchcase
from itertools import combinations
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/checks'))
from run import discover

STAGE = re.compile(r'(?:^|[-_])((?:phase|stage|step|sep|tt|[ptskdfbh])[-_]?\d+[a-z]?)(?=$|[-_])')
RULES = {
    '段名': 'CHECK.name の区切られた段番号に一致 ' + STAGE.pattern,
    '模型': '部分試験で covers の試験 C 単独から実行ファイルを作り実行。C に include がなく、試験 Python に製品ソースの入力がない',
    'コンパイルのみ': '登録から直接呼ぶ全試験でプロセス呼出しがコンパイラの -fsyntax-only / -c だけと解決できる',
    '同一宣言': '空でない covers と basis の各文字列集合が完全一致する全ペア',
    '変異50超': 'CI の個別 RED 行が >50。未計測なら静的候補: 登録本体または run が直接指す Python の for 文で表の列を unpack し、'
               '同じループの replace の前後引数に渡す相異なる文字列の行、または各項目を渡して'
               '失敗を assert し RED を出すループの相異なるリテラル行。単一表で >50',
}
UNKNOWN = '未確定'


def red_loop(loop, item):
    """Look for result = call(...item...), assert result, print('RED ...')."""
    results = set()
    asserted = set()
    printed = False
    for stmt in loop.body:
        if isinstance(stmt, ast.Assign) and isinstance(stmt.value, ast.Call):
            if any(isinstance(n, ast.Name) and n.id == item for n in ast.walk(stmt.value)):
                results.update(t.id for t in stmt.targets if isinstance(t, ast.Name))
        if isinstance(stmt, ast.Assert) and isinstance(stmt.test, ast.Name):
            asserted.add(stmt.test.id)
        if isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Call):
            call = stmt.value
            if ast.unparse(call.func) == 'print' and call.args and isinstance(call.args[0], ast.Constant):
                printed |= str(call.args[0].value).startswith('RED')
    return printed and bool(results & asserted)


def mutation_tables(tree):
    assignments = defaultdict(list)
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name):
                    assignments[target.id].append(node.value)
    for loop in ast.walk(tree):
        if not isinstance(loop, ast.For):
            continue
        items, fields = loop.iter, loop.target
        if isinstance(items, ast.Call) and ast.unparse(items.func) == 'enumerate' and items.args:
            items = items.args[0]
            if isinstance(fields, ast.Tuple) and len(fields.elts) == 2:
                fields = fields.elts[1]
        if not isinstance(items, ast.Name):
            continue
        values = assignments[items.id]
        if len(values) != 1 or not isinstance(values[0], (ast.List, ast.Tuple)):
            continue
        if isinstance(fields, ast.Name) and red_loop(loop, fields.id):
            rows = {ast.dump(row) for row in values[0].elts if isinstance(row, ast.Tuple)
                    and all(isinstance(cell, ast.Constant) for cell in row.elts)}
            if len(rows) > 50:
                yield f'{items.id}:{values[0].lineno} RED loop:{loop.lineno} 入力行={len(rows)}'
        if not isinstance(fields, ast.Tuple):
            continue
        columns = [ast.unparse(f) for f in fields.elts]
        for call in ast.walk(loop):
            if not (isinstance(call, ast.Call) and isinstance(call.func, ast.Attribute)
                    and call.func.attr == 'replace' and len(call.args) >= 2):
                continue
            args = [ast.unparse(arg) for arg in call.args[:2]]
            if not all(arg in columns for arg in args):
                continue
            left, right = map(columns.index, args)
            count = 0
            for row in values[0].elts:
                if isinstance(row, (ast.Tuple, ast.List)) and len(row.elts) == len(columns):
                    a, b = row.elts[left], row.elts[right]
                    if all(isinstance(v, ast.Constant) and isinstance(v.value, str) for v in (a, b)):
                        count += a.value != b.value
            if count > 50:
                yield f'{items.id}:{values[0].lineno} for:{loop.lineno} replace:{call.lineno} 置換行={count}'
                break


def test_source(path):
    return path.startswith(('tools/tests/', 'tools/ppc/', 'tools/accept/'))


def literal(node):
    return node.value if isinstance(node, ast.Constant) and isinstance(node.value, str) else ''


def arguments(node, names, seen=frozenset()):
    """Resolve literal argv fragments. Keep opaque expressions distinct from strings."""
    if isinstance(node, ast.Name) and node.id in names and node.id not in seen:
        return arguments(names[node.id], names, seen | {node.id})
    if isinstance(node, (ast.List, ast.Tuple)):
        return [v for cell in node.elts for v in arguments(cell, names, seen)]
    if isinstance(node, ast.Starred):
        return arguments(node.value, names, seen)
    if isinstance(node, ast.Call) and ast.unparse(node.func) == 'str' and len(node.args) == 1:
        return arguments(node.args[0], names, seen)
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Div) and literal(node.right):
        return [literal(node.right)]
    return [literal(node) or '<' + ast.unparse(node) + '>']


def probe(root, path, script, covers, delegates):
    body = [stmt for stmt in script.body if not (isinstance(stmt, ast.Assign)
            and any(ast.unparse(t) == 'CHECK' for t in stmt.targets))]
    nodes = [n for stmt in body for n in ast.walk(stmt)]
    assigned = defaultdict(list)
    for n in nodes:
        if isinstance(n, ast.Assign):
            for target in n.targets:
                if isinstance(target, ast.Name):
                    assigned[target.id].append(n.value)
    names = {key: vals[0] for key, vals in assigned.items() if len(vals) == 1}
    syntax, builds, runs, other = [], [], [], []
    for n in nodes:
        if not isinstance(n, ast.Call) or not n.args:
            continue
        func = ast.unparse(n.func)
        if not (func.startswith(('subprocess.', 'host32.')) or
                func.split('.')[-1] in {'run', 'runner', 'run_cmd', 'Popen', 'check_call',
                                         'check_output', 'system', 'execute', 'call'}):
            continue
        argv = arguments(n.args[0], names)
        if not argv:
            other.append(n.lineno)
            continue
        cmd = argv[0]
        if cmd in ('python3', '<sys.executable>') and (
                set(argv) & delegates or '<__file__>' in argv or
                'Path(__file__).resolve()' in cmd + ' '.join(argv)):
            continue
        if cmd.endswith(('gcc', 'clang')) or cmd == 'cc':
            if '-fsyntax-only' in argv or '-c' in argv:
                syntax.append(f'{path}:{n.lineno} {cmd} ' + ('-c' if '-c' in argv else '-fsyntax-only'))
            elif '-o' in argv and argv.index('-o') + 1 < len(argv):
                builds.append((argv, argv[argv.index('-o') + 1], n.lineno))
            else:
                other.append(n.lineno)
        else:
            runs.append((cmd, n.lineno))
            other.append(n.lineno)
    models = []
    product = [c for c in covers if not test_source(c)]
    strings = {literal(n) for n in nodes if literal(n)}
    if not any(fnmatchcase(v, pat) or v == Path(pat).name for v in strings for pat in product):
        for argv, output, line in builds:
            inputs = [v for v in argv if v.endswith(('.c', '.cc', '.o', '.a', '.S'))]
            if len(inputs) != 1 or not test_source(inputs[0]) or inputs[0] not in covers:
                continue
            if any(v.startswith(('<', '-l', '-include', '@')) for v in argv):
                continue
            cfile = root / inputs[0]
            if not cfile.is_file():
                continue
            code = re.sub(r'/\*.*?\*/|//[^\n]*', '', cfile.read_text(), flags=re.S)
            if re.search(r'^\s*#\s*include', code, re.M):
                continue
            for cmd, runline in runs:
                if cmd == output:
                    models.append(f'{path}:{line} {inputs[0]} 単独リンク (include なし); {path}:{runline} 生成物実行')
    return syntax, not other and not builds, models


def inventory(root, checks, measurements):
    groups = defaultdict(list)
    for c in checks.values():
        if c.covers and c.basis:
            groups[frozenset(c.covers), frozenset(c.basis)].append(c.name)
    pairs = defaultdict(list)
    for members in groups.values():
        for a, b in combinations(sorted(members), 2):
            pairs[a].append(b)
            pairs[b].append(a)

    for name, c in sorted(checks.items()):
        tree = ast.parse((root / c.path).read_bytes())
        entry = next(f for f in tree.body if isinstance(f, ast.FunctionDef) and f.name == 'run')
        scripts = {c.path}
        for n in ast.walk(entry):
            if isinstance(n, ast.Constant) and isinstance(n.value, str) and test_source(n.value):
                if n.value.endswith('.py') and (root / n.value).is_file():
                    scripts.add(n.value)
        hits = defaultdict(list)
        match = STAGE.search(name)
        if match:
            hits['段名'].append(f'{c.path} CHECK.name={name} 一致={match[1]}')
        syntax = []
        compile_only = True
        for path in sorted(scripts):
            script = tree if path == c.path else ast.parse((root / path).read_bytes())
            flags, resolved, models = probe(root, path, script, c.covers, scripts)
            compile_only &= resolved and (bool(flags) or (path == c.path and len(scripts) > 1))
            syntax.extend(flags)
            hits['模型'].extend(models)
            for evidence in mutation_tables(script):
                hits['変異50超'].append(f'{path} {evidence}')
        if compile_only:
            hits['コンパイルのみ'].extend(syntax)
        for other in pairs[name]:
            hits['同一宣言'].append(f'{other} ({checks[other].path})')
        for kind in RULES:
            if kind == '変異50超' and name in measurements:
                count, source = measurements[name]
                yield (kind, name, '候補' if count > 50 else '条件外', source,
                       source.split('-')[0], '個別の RED の行', count)
                continue
            if hits[kind]:
                yield kind, name, '候補', ' | '.join(hits[kind]), '', '', ''
            elif kind == '段名' or (kind == '同一宣言' and c.covers and c.basis):
                yield kind, name, '条件外', f'{c.path} CHECK 宣言を比較', '', '', ''
            else:
                yield kind, name, UNKNOWN, f'{c.path} ' + (
                    '比較する covers / basis が空' if kind == '同一宣言' else '条件を満たす静的根拠なし'), '', '', ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--checks-tsv', type=Path, required=True)
    args = parser.parse_args()
    root = args.root
    with args.checks_tsv.open(newline='') as stream:
        measurements = {r['name']: (int(r['mutation_count']), r['mutation_source'])
                        for r in csv.DictReader(stream, delimiter='\t') if r['mutation_basis'] == 'ci-red'}
    for kind, condition in RULES.items():
        print(f'# {kind}: {condition}')
    print('# 計測の版: CI run 45568507923・45568508091。計測値は参照ログの個別 RED 行。静的候補には計測値なし。')
    writer = csv.writer(sys.stdout, delimiter='\t', lineterminator='\n')
    writer.writerow(('条件', '登録名', '状態', '根拠', '計測 CI run', '単位', '計測数'))
    writer.writerows(inventory(root, discover(root), measurements))


if __name__ == '__main__':
    main()
