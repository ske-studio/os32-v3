#!/usr/bin/env python3
"""split_c_functions.py — 対応表に従って C の関数・case を .inc に機械的に移す。

範囲は clang の AST (-ast-dump=json) から取る。人が決めるのは対応表だけ
(docs/POLICY_DEV.md §2「分割の手段」)。移した行は元の位置の
`#include "..."` に置き換わり、同じ翻訳単位のまま機械語が保たれる。

  python3 tools/split_c_functions.py SRC MAP.json [--dry-run] -- <clang の -D/-I/-target など>

MAP.json は {キー: 移す先 (SRC の置き場からの相対)}。キーは
  名前          top-level の宣言 1 つ (関数・変数・型)
  名前..名前    最初の宣言の先頭から最後の宣言の末尾まで (間のマクロ・静的検査も含む)
  関数[#n]:ラベル  関数の n 番目 (1 起点、既定 1) の switch の `case ラベル:` で始まる
                case から次の case/default の直前まで (複数ラベルの行はまとめて動く)
宣言の直前のコメントは直前の空行まで含める。最後に include を展開し直して
元の全原文と一致すること (どの行も 1 回だけ残ること) を確かめる。
"""
import json, pathlib, re, subprocess, sys

def ast(src, args):
    out = subprocess.run(['clang', '-Xclang', '-ast-dump=json', '-fsyntax-only', *args, str(src)],
                         check=True, capture_output=True, text=True).stdout
    tree, last = json.loads(out), {'file': None, 'line': None}
    def walk(x):  # JSON は直前と同じ file/line を省くので、出力順にたどって補う
        if isinstance(x, dict):
            if 'offset' in x and ('col' in x or 'line' in x):
                last['file'] = x.get('file', last['file']); last['line'] = x.get('line', last['line'])
                x['_file'], x['_line'] = last['file'], last['line']
            for v in x.values(): walk(v)
        elif isinstance(x, list):
            for v in x: walk(v)
    walk(tree)
    return tree

def line(loc):  # マクロ展開の中なら展開した位置
    loc = loc.get('expansionLoc', loc)
    return loc.get('_file'), loc.get('_line')

def extent(node):
    return line(node['range']['begin'])[1], line(node['range']['end'])[1]

def switches(node):
    for c in node.get('inner', []):
        if c.get('kind') == 'SwitchStmt': yield c
        yield from switches(c)

def resolve(key, decls, text):
    if ':' in key:
        fn, label = key.split(':', 1)
        fn, n = (fn.split('#') + ['1'])[:2]
        body = list(switches(decls[fn]))[int(n) - 1]['inner'][-1]
        labels = [c for c in body['inner'] if c['kind'] in ('CaseStmt', 'DefaultStmt')]
        starts = [extent(c)[0] for c in labels] + [extent(body)[1]]
        for i, s in enumerate(starts[:-1]):
            if re.match(r'\s*case\s+%s\s*:' % re.escape(label), text[s - 1]):
                for t in starts[i:i + 2]:
                    if not re.match(r'\s*(case\b|default\b|\})', text[t - 1]): sys.exit(f'{key}: line {t} shares a label')
                return s, starts[i + 1] - 1
        sys.exit(f'{key}: no such case')
    first, last = (key.split('..') + [key])[:2] if '..' in key else (key, key)
    s, e = extent(decls[first])[0], extent(decls[last])[1]
    if text[s - 1][:1].isspace(): sys.exit(f'{key}: does not start a line')
    if not text[e - 1].rstrip().endswith((';', '}')): sys.exit(f'{key}: line {e} continues')
    while s > 1 and re.match(r'\s*(/\*|\*|//)', text[s - 2]): s -= 1  # 直前のコメント
    return s, e

def main(argv):
    dry = '--dry-run' in argv
    sep = argv.index('--') if '--' in argv else len(argv)
    pos = [a for a in argv[1:sep] if a != '--dry-run']
    src, table = pathlib.Path(pos[0]), json.loads(pathlib.Path(pos[1]).read_text())
    original = src.read_text()
    text = original.split('\n')
    tree = ast(src, argv[sep + 1:])
    decls = {n['name']: n for n in tree['inner'] if 'name' in n and 'range' in n
             and line(n['range']['begin'])[0] == str(src) and n.get('isImplicit') is not True}
    spans = sorted((resolve(k, decls, text) + (k, d) for k, d in table.items()), reverse=True)
    for (s, e, k, d), nxt in zip(spans, spans[1:] + [None]):
        if nxt and nxt[1] >= s: sys.exit(f'{k} overlaps {nxt[2]}')
    files = {}
    for s, e, k, d in spans:  # 後ろから置き換えるので行番号はずれない
        print(f'{src}:{s}-{e} ({e - s + 1} lines) {k} -> {d}')
        if d in files: sys.exit(f'{d}: one key per file')
        files[d] = '\n'.join(text[s - 1:e]) + '\n'
        indent = re.match(r'\s*', text[s - 1]).group() if ':' in k else ''
        text[s - 1:e] = [f'{indent}#include "{d}"']
    result = '\n'.join(text)
    def expand(t):
        return re.sub(r'^[ \t]*#include "([^"]+)"\n', lambda m: files.get(m.group(1), m.group(0)),
                      t, flags=re.M)
    if expand(result) != original:  # 移した行は include 1 行に替わったので、一致すれば各行は 1 回だけ
        sys.exit('expanded result differs from the original')
    if dry: return 0
    for d, body in files.items():
        p = src.parent / d
        if p.exists(): sys.exit(f'{p} exists')
        p.parent.mkdir(parents=True, exist_ok=True); p.write_text(body)
    src.write_text(result)
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
