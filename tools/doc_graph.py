#!/usr/bin/env python3
"""文書の木の検査 (Python 標準ライブラリのみ)。

相対 .md リンクの出現を数える (同じ宛先の再掲も 1 本ずつ)。相互リンクは
文書の組ごとに 1 件。アンカーの実在は check-docs-links に任せる。
宣言と状態行は既存の状態検査と同じ先頭 12 行。索引は `> 索引:` の有無、
親・後続は `> 上位:` / `> 後続:` の Markdown リンクで指定する。
L1〜L4 → L5 はリンクを正典への参照として検査する (意味の自然言語推測はしない)。
--update-baseline だけが基準値を書き換える。通常実行は一覧と比較を出す。
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
HEAD_LINES = 12
KINDS = {
    'spec_to_ticket': '仕様→票',
    'decision_to_ticket': '決定→票',
    'ticket_crosslink': '票→票の上位・後続以外',
    'mutual_non_parent': '親子以外の相互リンク',
    'current_to_archive': '現行→アーカイブ',
    'canonical_to_record': 'L1〜L4→L5 の正典参照',
}
# 除外パスはディレクトリ表記の末尾 / を正規化する。
EXCLUDED = {name.rstrip('/') for name in ('.git', 'build/', 'apps', 'game', 'docs/hw')}
INLINE = re.compile(r'!?\[[^\]\n]*\]\(\s*(<[^>\n]+>|(?:[^\s()]|\([^()]*\))+)(?:\s+"[^"\n]*")?\s*\)')
REFDEF = re.compile(r'^ {0,3}\[([^]\n]+)\]:\s*(<[^>\n]+>|\S+)')
REFUSE = re.compile(r'(?<!!)\[([^]\n]+)\](?:\[([^]\n]*)\])?(?!\()')
STATUS = re.compile(r'(?:^|> |/ )状態:\s*(.*)$')
DECL = re.compile(r'^> (索引|上位|後続):\s*(.*)$')


def prose(text):
    """コード例・HTML コメントをリンクや宣言として読まない。行番号は保つ。"""
    text = re.sub(r'<!--[\s\S]*?-->', lambda m: '\n' * m[0].count('\n'), text)
    out, fence = [], None
    for line in text.splitlines():
        mark = re.match(r'^ {0,3}(`{3,}|~{3,})', line)
        if mark:
            if fence is None:
                fence = mark[1]
            elif mark[1][0] == fence[0] and len(mark[1]) >= len(fence):
                fence = None
            out.append('')
        elif fence:
            out.append('')
        else:
            out.append(re.sub(r'(`+).*?\1', '', line))
    return out


def classify(path, lines):
    if path.startswith('docs/archive/'):
        return 9
    if path in {'CLAUDE.md', 'AGENTS.md', 'README.md', 'INSTALL.md', 'docs/INDEX.md'}:
        return 0
    if (path in {'docs/CONSTRAINTS.md', 'docs/tasks/agents/ROLES.md'}
            or path.startswith(('docs/POLICY_', '.claude/'))):
        return 1
    if (path == 'docs/tasks/DEFERRED_TESTS.md'
            or path.startswith('docs/tasks/agents/')
            or (path.startswith('tools/tests/') and path.endswith('_tdd.md'))):
        return 5
    status = next((m[1].lstrip('* ').strip() for line in lines[:HEAD_LINES]
                   if (m := STATUS.search(line))), '')
    if path.startswith('docs/decisions/') or re.match(r'確定(?:$|[\s*(（—。、])', status):
        return 3
    if path.startswith('docs/tasks/') or path in {'docs/ROADMAP.md', 'docs/V3_PLAN.md'}:
        return 4
    return 2


def git_tracked_md(root):
    """root が git の作業木の頂点なら管理している .md の相対パス、そうでなければ None (合成の試験の木)。"""
    if not (root / '.git').exists():
        return None
    try:
        out = subprocess.run(['git', '-C', str(root), 'ls-files', '-z', '--', '*.md'],
                             capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return sorted(p for p in out.decode('utf-8').split('\0') if p)


def documents(root):
    if not root.is_dir():
        raise ValueError('対象のルートがありません: ' + str(root))
    result = {}
    tracked = git_tracked_md(root)
    if tracked is not None:
        # git の作業木では管理しているファイルだけを数える (管理外の写し .agents/ などを拾わない)。
        for rel in tracked:
            path = root / rel
            parents_link = any((root / q).is_symlink() for q in Path(rel).parents if str(q) != '.')
            if (path.suffix == '.md' and not path.is_symlink() and not parents_link and path.is_file()
                    and not any(rel == e or rel.startswith(e + '/') for e in EXCLUDED)):
                result[rel] = path.read_text(encoding='utf-8').splitlines()
        return result
    for directory, dirs, files in os.walk(root):
        base = Path(directory)
        dirs[:] = sorted(d for d in dirs if not (base / d).is_symlink()
                         and (base / d).relative_to(root).as_posix() not in EXCLUDED)
        for name in sorted(files):
            path = base / name
            if path.suffix == '.md' and not path.is_symlink():
                rel = path.relative_to(root).as_posix()
                result[rel] = path.read_text(encoding='utf-8').splitlines()
    return result


def links(source, lines, paths):
    """(行番号, 宛先)。参照形式も使用箇所で数え、定義そのものは数えない。"""
    definitions = {m[1].casefold(): m[2] for line in lines if (m := REFDEF.match(line))}
    for no, line in enumerate(lines, 1):
        if REFDEF.match(line):
            continue
        targets = [m[1] for m in INLINE.finditer(line)]
        remainder = INLINE.sub('', line)
        for m in REFUSE.finditer(remainder):
            key = (m[2] or m[1]).casefold()
            if key in definitions:
                targets.append(definitions[key])
        for target in targets:
            url = urlsplit(target.strip('<>'))
            if url.scheme or url.netloc or url.path.startswith('/'):
                continue
            path = unquote(url.path)
            if not path.endswith('.md'):
                continue
            dest = os.path.normpath(os.path.join(os.path.dirname(source), path))
            if dest in paths and dest != source:
                yield no, dest


def analyze(root):
    raw_docs = documents(Path(root).resolve())
    docs = {p: prose('\n'.join(lines)) for p, lines in raw_docs.items()}
    layers = {p: classify(p, lines) for p, lines in docs.items()}
    edges = {p: list(links(p, lines, docs)) for p, lines in docs.items()}
    indexes, parents, successors = set(), {}, {}
    for source, lines in docs.items():
        parents[source], successors[source] = set(), set()
        for no, line in enumerate(lines[:HEAD_LINES], 1):
            match = DECL.match(line)
            if not match:
                continue
            kind, value = match.groups()
            if kind == '索引':
                indexes.add(source)
            elif kind in ('上位', '後続') and layers[source] == 4:
                targets = {dest for n, dest in edges[source] if n == no}
                (parents if kind == '上位' else successors)[source].update(targets)
    violations = {kind: [] for kind in KINDS}
    for source, outgoing in edges.items():
        a = layers[source]
        if a == 9:
            continue
        for no, dest in outgoing:
            b = layers[dest]
            detail = f'{source}:{no} → {dest}'
            # [D4]: 索引・記録・経緯も含め、全現行層から禁止する。
            if b == 9:
                violations['current_to_archive'].append(detail)
            if source not in indexes:
                if a == 2 and b == 4:
                    violations['spec_to_ticket'].append(detail)
                if a == 3 and b == 4:
                    violations['decision_to_ticket'].append(detail)
                if a == 4 and b == 4 and dest not in parents[source] | successors[source]:
                    violations['ticket_crosslink'].append(detail)
                if a in (1, 2, 3, 4) and b == 5 and not (
                        a == 4 and dest == 'docs/tasks/DEFERRED_TESTS.md'):
                    violations['canonical_to_record'].append(detail)
            # 相互リンクは出現数によらず 1 組。アーカイブからの辺は検査対象外。
            if source < dest and b != 9 and any(t == source for _, t in edges[dest]):
                parent_child = ((source in indexes and source in parents[dest])
                                or (dest in indexes and dest in parents[source]))
                pair = f'{source} ↔ {dest}'
                if not parent_child and pair not in violations['mutual_non_parent']:
                    violations['mutual_non_parent'].append(pair)
    return violations


def read_baseline(path):
    data = json.loads(path.read_text(encoding='utf-8'))
    if (not isinstance(data, dict) or set(data) != set(KINDS)
            or any(type(n) is not int or n < 0 for n in data.values())):
        raise ValueError('基準値は全種類の非負整数が必要です')
    return data


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--baseline', type=Path, help='既定: ROOT/tools/doc_tree_baseline.json')
    parser.add_argument('--update-baseline', action='store_true')
    args = parser.parse_args(argv)
    baseline_path = args.baseline or args.root / 'tools/doc_tree_baseline.json'
    try:
        violations = analyze(args.root)
        counts = {kind: len(items) for kind, items in violations.items()}
        if args.update_baseline:
            baseline_path.write_text(json.dumps(counts, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            print('基準値を更新: ' + str(baseline_path))
        baseline = read_baseline(baseline_path)
        for kind, items in violations.items():
            print(f'{kind} ({KINDS[kind]}): {counts[kind]} / 基準値 {baseline[kind]}')
            for item in items:
                print('  ' + item)
            if counts[kind] < baseline[kind]:
                print('  基準値を下げてよい (--update-baseline)')
        increased = any(counts[k] > baseline[k] for k in KINDS)
        print('NG — 基準値より増加' if increased else 'OK — 文書の木は基準値以下')
        return int(increased)
    except (OSError, ValueError) as exc:
        print('Error: ' + str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
