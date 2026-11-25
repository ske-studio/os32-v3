"""P0-7: emit review leads and their source evidence from a registration tree."""
import argparse
import ast
from collections import defaultdict
import csv
from functools import lru_cache
from itertools import combinations
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/checks'))
from run import discover

STAGE = re.compile(r'(?:^|[-_])((?:phase|stage|step|sep|tt|[ptskdfbh])[-_]?\d+[a-z]?)(?=$|[-_])')
MODEL = re.compile(r'純粋な模型|\b(?:pure|standalone) (?:state )?model\b', re.I)
COMPILE = re.compile(r'\bcompile[- ]only\b|コンパイル(?:確認)?(?:だけ|のみ)', re.I)
RULES = {
    '段名': 'CHECK.name の区切られた段番号に一致 ' + STAGE.pattern,
    '模型': 'run から直接たどる試験 Python またはそこに明記された試験 C に純粋・独立模型の明記 ' + MODEL.pattern,
    'コンパイルのみ': '直接たどった試験に -fsyntax-only を持つコンパイラ引数列、または限定を明記 ' + COMPILE.pattern,
    '同一宣言': '空でない covers と basis の各文字列集合が完全一致する全ペア',
    '変異50超': '登録本体または run が直接指す Python の for 文で表の列を unpack し、'
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


def inventory(root, checks):
    @lru_cache(maxsize=None)
    def inspect(path):
        source = (root / path).read_text()
        lines = source.splitlines()
        hits = {}
        for kind, pattern in [('模型', MODEL), ('コンパイルのみ', COMPILE)]:
            for i, line in enumerate(lines, 1):
                if pattern.search(line):
                    hits[kind] = f'{path}:{i} {line.strip()}'
                    break
        return hits

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
        sources = set(scripts)
        for path in sorted(scripts):
            script = tree if path == c.path else ast.parse((root / path).read_bytes())
            for stmt in script.body:
                if isinstance(stmt, ast.Assign) and any(ast.unparse(t) == 'CHECK' for t in stmt.targets):
                    continue
                for n in ast.walk(stmt):
                    if isinstance(n, ast.Constant) and isinstance(n.value, str) and test_source(n.value):
                        if n.value.endswith('.c') and (root / n.value).is_file():
                            sources.add(n.value)
            for node in ast.walk(script):
                if isinstance(node, ast.List) and test_source(path):
                    flags = [cell.value for cell in node.elts
                             if isinstance(cell, ast.Constant) and isinstance(cell.value, str)]
                    if '-fsyntax-only' in flags and any(
                            f.startswith('-std=') or f.endswith(('gcc', 'clang')) for f in flags):
                        hits['コンパイルのみ'].append(f'{path}:{node.lineno} コンパイラ引数 -fsyntax-only')
                        break
            for evidence in mutation_tables(script):
                hits['変異50超'].append(f'{path} {evidence}')
        for path in sorted(sources):
            if test_source(path):
                for kind, evidence in inspect(path).items():
                    hits[kind].append(evidence)
        for other in pairs[name]:
            hits['同一宣言'].append(f'{other} ({checks[other].path})')
        for kind in RULES:
            if hits[kind]:
                yield kind, name, '候補', ' | '.join(hits[kind])
            elif kind == '段名' or (kind == '同一宣言' and c.covers and c.basis):
                yield kind, name, '条件外', f'{c.path} CHECK 宣言を比較'
            else:
                yield kind, name, UNKNOWN, f'{c.path} ' + (
                    '比較する covers / basis が空' if kind == '同一宣言' else '条件を満たす静的根拠なし')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    root = parser.parse_args().root
    for kind, condition in RULES.items():
        print(f'# {kind}: {condition}')
    print('# 模型・コンパイルの根拠は部分試験を含む。変異は静的置換行数、実行総数は未確認。')
    writer = csv.writer(sys.stdout, delimiter='\t', lineterminator='\n')
    writer.writerow(('条件', '登録名', '状態', '根拠'))
    writer.writerows(inventory(root, discover(root)))


if __name__ == '__main__':
    main()
