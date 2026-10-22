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
from dataclasses import dataclass
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
DESTINATION = re.compile(r'\(\s*(<[^>\n]+>|(?:[^\s()]|\([^()]*\))+)(?:\s+"[^"\n]*")?\s*\)')
REFDEF = re.compile(r'^ {0,3}\[((?:\\.|[^]\\\n])+)\]:\s*(<[^>\n]+>|\S+)')
STATUS = re.compile(r'(?:^|> |/ )状態:\s*(.*)$')
DECL = re.compile(r'^> (索引|上位|後続):\s*(.*)$')


def normalize_reference(label):
    """CommonMark の参照名: Unicode case folding と空白の畳み込み。"""
    return ' '.join(label.split()).casefold()


def escaped(text, pos):
    start = pos
    while start and text[start - 1] == '\\':
        start -= 1
    return (pos - start) % 2 == 1


def code_end(text, start):
    """同じ長さのバッククォート列で閉じるコードスパン (無ければ None)。"""
    run = re.match(r'`+', text[start:])[0]
    for match in re.finditer(r'`+', text[start + len(run):]):
        if len(match[0]) == len(run):
            return start + len(run) + match.end()
    return None


def reference_definitions(lines, visible):
    definitions, locations = {}, {}
    for index, line in enumerate(visible):
        match = REFDEF.match(line)
        if match:
            # ラベル中のコードも、定義では参照名そのもの。
            raw = lines[index]
            key = normalize_reference(raw[match.start(1):match.end(1)])
            target = raw[match.start(2):match.end(2)]
            definitions.setdefault(key, target)  # CommonMark: 最初の定義が有効
            locations[index] = (key, target)
    return definitions, locations


@dataclass
class Link:
    start: int
    end: int
    label_start: int
    label_end: int
    target: str
    key: str
    image: bool
    children: list
    reason: str = ''


def scan_links(line, visible, definitions, start=0, end=None):
    """位置つきのリンク木。未対応の候補は範囲全体を reason 付きで残す。

    1 行内のラベル・宛先に対応。コードスパン、エスケープ、入れ子の画像を
    ラベル境界で考慮し、部分一致で参照の末尾だけを拾わない。
    """
    end = len(line) if end is None else end
    found, pos = [], start
    while pos < end:
        image = line.startswith('![', pos) and not escaped(line, pos)
        opening = pos + int(image)
        if (opening >= end or visible[opening:opening + 1] != '['
                or escaped(line, opening)):
            pos += 1
            continue
        cursor, depth = opening + 1, 1
        while cursor < end:
            char = line[cursor]
            if escaped(line, cursor):
                cursor += 1
                continue
            if char == '`':
                cursor = (code_end(line[:end], cursor)
                          or cursor + len(re.match(r'`+', line[cursor:])[0]))
                continue
            if char == '[':
                depth += 1
            elif char == ']':
                depth -= 1
                if not depth:
                    break
            cursor += 1
        if depth:
            # 閉じない [ は文字 (半開区間の表記など)。後続のリンクは走査する。
            pos += 1
            continue
        label = line[opening + 1:cursor]
        stop, target, key, reason = cursor + 1, '', '', ''
        if line[stop:stop + 1] == '(':
            match = DESTINATION.match(line, stop, end)
            if match:
                stop, target = match.end(), match[1]
            else:
                stop, reason = end, '複数行または未対応の宛先'
        elif line[stop:stop + 1] == '[':
            ref_end = stop + 1
            while ref_end < end and (line[ref_end] != ']' or escaped(line, ref_end)):
                ref_end += 1
            if ref_end == end:
                stop, reason = end, '複数行または閉じていない参照名は未対応'
            else:
                key = normalize_reference(line[stop + 1:ref_end] or label)
                stop, target = ref_end + 1, definitions.get(key, '')
        else:
            key = normalize_reference(label)
            target = definitions.get(key, '')
        children = scan_links(line, visible, definitions, opening + 1, cursor)
        if target and not image and any(child.target and not child.image for child in children):
            reason = '画像以外のリンクを内包するラベルは未対応'
        found.append(Link(pos, stop, opening + 1, cursor, target, key, image, children, reason))
        pos = stop
    return found


def link_rows(lines, visible, definitions, locations):
    """未対応候補がある段落は全体を保持し、次行の参照だけを変換しない。"""
    def reason(node):
        return node.reason or next((value for child in node.children
                                    if (value := reason(child))), '')

    def flush(rows):
        issues = [(index, nodes, next((value for node in nodes
                                      if (value := reason(node))), ''))
                  for index, nodes in rows]
        # 複数行リンクの末尾だけを単独の参照として扱わない。
        raw = '\n'.join(lines[index].rstrip('\r\n') for index, _ in rows)
        mask = '\n'.join(visible[index] for index, _ in rows)
        for node in scan_links(raw, mask, definitions):
            if (node.target or node.reason) and '\n' in raw[node.start:node.end]:
                row = raw[:node.start].count('\n')
                index, nodes, _ = issues[row]
                issues[row] = (index, nodes, '複数行のリンクは未対応')
        unsupported = any(issue for _, _, issue in issues)
        for index, nodes, issue in issues:
            yield index, [] if unsupported else nodes, issue

    rows = []
    for index, line in enumerate(visible):
        if index in locations or not line.strip():
            yield from flush(rows)
            rows = []
        if index not in locations and line.strip():
            rows.append((index, scan_links(lines[index], line, definitions)))
    yield from flush(rows)


def prose(text, preserve_columns=False):
    """コード例・HTML コメントを読まない。preserve_columns は編集用に桁も保つ。"""
    def hide(value):
        return re.sub(r'[^\r\n]', ' ', value) if preserve_columns else '\n' * value.count('\n')

    comments = r'<!--[\s\S]*?-->'
    fence_lines = re.sub(comments, lambda m: '\n' * m[0].count('\n'), text).splitlines()
    text = re.sub(comments, lambda m: hide(m[0]), text)
    out, fence = [], None
    for index, line in enumerate(text.splitlines()):
        fence_line = fence_lines[index] if index < len(fence_lines) else ''
        mark = re.match(r'^ {0,3}(`{3,}|~{3,})', fence_line)
        if mark:
            if fence is None:
                fence = mark[1]
            elif mark[1][0] == fence[0] and len(mark[1]) >= len(fence):
                fence = None
            out.append(hide(line))
        elif fence:
            out.append(hide(line))
        else:
            parts, pos = [], 0
            while pos < len(line):
                if (line[pos] == '`' and not escaped(line, pos)
                        and (end := code_end(line, pos)) is not None):
                    parts.append(hide(line[pos:end]))
                    pos = end
                else:
                    width = len(re.match(r'`+', line[pos:])[0]) if line[pos] == '`' else 1
                    parts.append(line[pos:pos + width])
                    pos += width
            out.append(''.join(parts))
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


def document_paths(root):
    """本文を読まずに対象を列挙する。D4 の道具は archive を読取前に除外できる。"""
    if not root.is_dir():
        raise ValueError('対象のルートがありません: ' + str(root))
    tracked = git_tracked_md(root)
    if tracked is not None:
        # git の作業木では管理しているファイルだけを数える (管理外の写し .agents/ などを拾わない)。
        for rel in tracked:
            path = root / rel
            parents_link = any((root / q).is_symlink() for q in Path(rel).parents if str(q) != '.')
            if (path.suffix == '.md' and not path.is_symlink() and not parents_link and path.is_file()
                    and not any(rel == e or rel.startswith(e + '/') for e in EXCLUDED)):
                yield rel
        return
    for directory, dirs, files in os.walk(root):
        base = Path(directory)
        dirs[:] = sorted(d for d in dirs if not (base / d).is_symlink()
                         and (base / d).relative_to(root).as_posix() not in EXCLUDED)
        for name in sorted(files):
            path = base / name
            if path.suffix == '.md' and not path.is_symlink():
                rel = path.relative_to(root).as_posix()
                yield rel


def documents(root):
    return {rel: (root / rel).read_text(encoding='utf-8').splitlines()
            for rel in document_paths(root)}


def links(source, lines, paths):
    """(行番号, 宛先)。参照形式も使用箇所で数え、定義そのものは数えない。"""
    visible = prose('\n'.join(lines), preserve_columns=True)
    definitions, locations = reference_definitions(lines, visible)

    def targets_in(nodes):
        for node in nodes:
            if node.reason:
                continue
            if node.target:
                yield node.target
            yield from targets_in(node.children)

    for index, nodes, _ in link_rows(lines, visible, definitions, locations):
        no = index + 1
        targets = targets_in(nodes)
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
    edges = {p: list(links(p, lines, docs)) for p, lines in raw_docs.items()}
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
