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

from doc_graph import INLINE, REFDEF, ROOT, document_paths, prose

ARCHIVE = 'docs/archive'
REFERENCE = re.compile(r'!?\[([^]\n]+)\](?:\[([^]\n]*)\])?(?!\()')


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


def delink(source, text):
    """位置を保った prose に一致した部分だけを置換し、変更一覧も返す。"""
    lines = text.splitlines(keepends=True)
    visible = prose(text, preserve_columns=True)
    definitions = {}
    definition_lines = {}
    for index, line in enumerate(visible):
        match = REFDEF.match(line)
        if match:
            target = lines[index][match.start(2):match.end(2)]
            key = match[1].casefold()
            definitions[key] = target
            definition_lines[index] = (key, target)

    changes, converted = [], set()
    result = list(lines)
    for index, line in enumerate(visible):
        if index in definition_lines:
            continue
        edits = []
        remainder = list(line)
        for match in INLINE.finditer(line):
            start, end = match.span()
            target = lines[index][match.start(1):match.end(1)]
            dest = archive_target(source, target)
            if dest is not None:
                original = lines[index][start:end]
                label = original[original.index('[') + 1:original.index(']')]
                edits.append((start, end, replacement(label, dest)))
            remainder[start:end] = ' ' * (end - start)
        for match in REFERENCE.finditer(''.join(remainder)):
            key = (match[2] or match[1]).casefold()
            dest = archive_target(source, definitions[key]) if key in definitions else None
            if dest is not None:
                start, end = match.span()
                original = lines[index][start:end]
                label = original[original.index('[') + 1:original.index(']')]
                edits.append((start, end, replacement(label, dest)))
                converted.add(key)
        for start, end, new in sorted(edits, reverse=True):
            changes.append(Change(index + 1, lines[index][start:end], new))
            result[index] = result[index][:start] + new + result[index][end:]

    # 同名の非 archive 定義や、置換後にも使用の残る定義は保持する。
    remaining = set()
    for index, line in enumerate(prose(''.join(result), preserve_columns=True)):
        if index not in definition_lines:
            remaining.update((m[2] or m[1]).casefold() for m in REFERENCE.finditer(line))
    for index, (key, target) in definition_lines.items():
        if key in converted and key not in remaining and archive_target(source, target) is not None:
            changes.append(Change(index + 1, lines[index].rstrip('\r\n'), '', True))
            result[index] = ''
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
        links = definitions = files = 0
        for rel in sorted(paths):
            if in_archive(rel):
                continue
            path = root / rel
            # 改行コードと EOF を維持する。
            with path.open(encoding='utf-8', newline='') as stream:
                original = stream.read()
            changed, edits = delink(rel, original)
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
        print(f'{"dry-run" if args.dry_run else "変更"}: リンク {links} 件、定義削除 {definitions} 件、文書 {files} 件')
        return 0
    except (OSError, ValueError) as exc:
        print('Error: ' + str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
