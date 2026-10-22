#!/usr/bin/env python3
"""現行 Markdown のアーカイブ宛相対リンクをパスの文字列へ戻す。

標準ライブラリのみ。アーカイブはパスだけを扱い、本文・見出しは読まない。
--paths はルートからのファイル名 (複数可)。件数はリンク置換と定義削除を別に数える。
"""
import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

from doc_graph import ROOT, document_paths, prose, reference_definitions, link_rows

ARCHIVE = 'docs/archive'


def in_archive(path):
    return path == ARCHIVE or path.startswith(ARCHIVE + '/')


def archive_target(source, target):
    url = urlsplit(target.strip('<>'))
    if url.scheme or url.netloc or url.path.startswith('/') or not url.path:
        return None
    dest = os.path.normpath(os.path.join(os.path.dirname(source), unquote(url.path)))
    return dest if in_archive(dest) else None


def replacement(label, dest):
    # 表の区切りになりうる文字は出力に持ち込まない (既存のラベルはそのまま)。
    code = '`' * (max((len(m[0]) for m in re.finditer(r'`+', dest)), default=0) + 1)
    literal = code + dest.replace('|', '&#124;') + code
    value = label.strip().strip('`')
    # 宛先のファイル名そのもの、または同じファイル名を持つパス表示。
    if value == Path(dest).name or ('/' in value and value.rsplit('/', 1)[-1] == Path(dest).name):
        return literal
    return f'{label} (アーカイブ: {literal})'


@dataclass
class Change:
    line: int
    old: str
    new: str
    definition: bool = False


def delink(source, text, skips=None):
    """位置を保った prose でリンク木を編集する。未対応の候補は丸ごと残す。"""
    lines = text.splitlines(keepends=True)
    visible = prose(text, preserve_columns=True)
    definitions, definition_lines = reference_definitions(lines, visible)
    changes, converted, remaining = [], set(), set()
    result = list(lines)
    skipped = []

    def render(node, line):
        label = line[node.label_start:node.label_end]
        # 内側を先に解決し、外側の置換と合わせて 1 個の編集にする。
        for child in reversed(node.children):
            value = render(child, line)
            start, end = child.start - node.label_start, child.end - node.label_start
            label = label[:start] + value + label[end:]
        dest = archive_target(source, node.target) if node.target else None
        if dest is not None:
            if node.key:
                converted.add(node.key)
            return replacement(label, dest)
        if node.key and node.target:
            remaining.add(node.key)
        return line[node.start:node.label_start] + label + line[node.label_end:node.end]

    for index, nodes, reason in link_rows(lines, visible, definitions, definition_lines):
        if reason:
            skipped.append((index + 1, reason))
            continue
        raw = lines[index]
        for node in reversed(nodes):
            new = render(node, raw)
            if new != raw[node.start:node.end]:
                changes.append(Change(index + 1, raw[node.start:node.end], new))
                result[index] = result[index][:node.start] + new + result[index][node.end:]

    # 未対応の候補が参照を使う可能性があるため、その文書の定義は保持する。
    for index, (key, target) in definition_lines.items():
        if (not skipped and key in converted and key not in remaining
                and archive_target(source, target) is not None):
            changes.append(Change(index + 1, lines[index].rstrip('\r\n'), '', True))
            result[index] = ''
    if skips is not None:
        skips.extend(sorted(skipped))
    return ''.join(result), sorted(changes, key=lambda c: c.line)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--paths', nargs='+', help='対象ファイル (ルートからの相対パス)')
    try:
        args = parser.parse_args(argv)
    except SystemExit as exc:
        return int(bool(exc.code))
    try:
        root = args.root.resolve()
        paths = set(document_paths(root))
        if args.paths is not None:
            requested = set()
            for value in args.paths:
                path = Path(value)
                path = path if path.is_absolute() else root / path
                # resolve() で symlink を選択対象へ取り込まない。
                rel = Path(os.path.abspath(path)).relative_to(root).as_posix()
                if rel not in paths:
                    raise ValueError('対象の文書ではありません: ' + value)
                requested.add(rel)
            paths &= requested
        links = definitions = files = skipped = 0
        for rel in sorted(paths):
            if in_archive(rel):
                continue
            path = root / rel
            # 改行コードと EOF を維持する。
            with path.open(encoding='utf-8', newline='') as stream:
                original = stream.read()
            skips = []
            changed, edits = delink(rel, original, skips)
            for line, reason in skips:
                print(f'skip {rel}:{line} {reason}')
            skipped += len(skips)
            if not edits:
                continue
            files += 1
            for edit in edits:
                if args.dry_run:
                    print(f'{rel}:{edit.line} {edit.old} → {edit.new}')
                definitions += int(edit.definition)
                links += int(not edit.definition)
            if not args.dry_run:
                with path.open('w', encoding='utf-8', newline='') as stream:
                    stream.write(changed)
        print(f'{"dry-run" if args.dry_run else "変更"}: リンク {links} 件、定義削除 {definitions} 件、文書 {files} 件、skip {skipped} 件')
        return 0
    except (OSError, ValueError) as exc:
        print('Error: ' + str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
