#!/usr/bin/env python3
"""Apply a [{"from": old, "to": new}, ...] layout map with git mv.

--dry-run emits moves, replacements, split-path warnings and regeneration targets
as JSON. Generated headers are rebuilt from their inputs, never text-edited.
"""
import argparse
import difflib
import json
import os
from pathlib import Path
import re
import subprocess

EXCLUDED = ('docs/archive/', 'docs/hw/', 'third_party/', 'lib/sqlite3/sqlite3.',
            'lib/zlib/', 'lib/microtar/', 'fs/fatfs/ff.', 'fs/fatfs/diskio.h',
            'userland/lib/ui/microui.', 'userland/lib/font/vendor/',
            'kdebug/generated/', 'hwdev/generated/', 'lib/sqlite3/generated/',
            'kapi/kapi_generated.c', 'exec/exec_kapi_init.inc',
            'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_kapi_generated.h',
            'sdk/include/os32/os32_kapi_slots.h', 'sdk/include/os32/os32_generations.h',
            'sdk/include/os32/os32_unit_stamp.h', 'sdk/os32_generations.py',
            'sdk/rust/os32api/src/generations.rs', 'sdk/crt/generations.inc',
            'sdk/link/generations.ld', 'sdk/link/app.ld', 'sdk/link/app_sys.ld',
            'sdk/link/shlib.ld', 'build/os32.ld')
BOUNDARY = r'A-Za-z0-9_./+\-'


def git(root, *args):
    return subprocess.run(['git', *args], cwd=root, check=True,
                          capture_output=True).stdout


def load_map(root, filename):
    moves = {}
    for row in json.loads(Path(filename).read_text()):
        old, new = row['from'], row['to']
        for name in (old, new):
            p = Path(name)
            if p.is_absolute() or '..' in p.parts or p.as_posix() != name:
                raise ValueError('map paths must be relative: ' + name)
        if old in moves or new in moves.values():
            raise ValueError('duplicate move: ' + old)
        if not (root / old).is_file() or (root / new).exists():
            raise ValueError('missing source / existing destination: ' + old + ' -> ' + new)
        moves[old] = new
    return moves


def path_pattern(paths):
    return re.compile(r'(?<![' + BOUNDARY + r'])(?:'
                      + '|'.join(re.escape(p) for p in sorted(paths, key=len, reverse=True))
                      + r')(?![' + BOUNDARY + r'])')


def rewrite(text, filename, moves, root=None):
    destination = moves.get(filename, filename)
    old_parent, new_parent = Path(filename).parent, Path(destination).parent
    replacements = dict(moves)
    for old, new in moves.items():
        if Path(old).suffix in ('.c', '.asm', '.S'):
            for suffix in ('.o', '.d'):
                replacements.setdefault(str(Path(old).with_suffix(suffix)), str(Path(new).with_suffix(suffix)))
    # Rebase local quoted includes even when their target itself does not move.
    def include(match):
        spelling = match[2]
        local = os.path.normpath(old_parent / spelling)
        target = moves.get(local, local)
        if local in moves or (old_parent != new_parent and (
                spelling.startswith('.') or (root is not None and (root / local).is_file()))):
            new = os.path.relpath(target, new_parent).replace(os.sep, '/')
            return match[1] + new + match[3]
        return match[0]
    text = re.sub(r'(^\s*#\s*include\s*")([^"\n]+)(")', include, text, flags=re.M)
    # Paths relative to the old file (e.g. ../kernel/paging.h) need a new base.
    relative = {}
    for old, new in replacements.items():
        rel = os.path.relpath(old, old_parent).replace(os.sep, '/')
        if rel.startswith('../'):
            relative[rel] = os.path.relpath(new, new_parent).replace(os.sep, '/')
    table = {**replacements, **relative}
    if table:
        text = path_pattern(table).sub(lambda m: table[m[0]], text)
    if filename == 'build/config.mk':
        added = {}
        for old, new in moves.items():
            old_dir, new_dir = str(Path(old).parent), str(Path(new).parent)
            if old_dir != new_dir:
                added.setdefault(old_dir, set()).add(new_dir)
        text = re.sub(r'-I([A-Za-z0-9_/]+)(?![A-Za-z0-9_/$({])',
                      lambda m: m[0] + ''.join(' -I' + d for d in sorted(added.get(m[1], ()))), text)
    return text


def plan(root, moves):
    names = git(root, 'ls-files', '-z').decode().split('\0')
    edits, split_paths = [], []
    for name in names:
        if not name or name.startswith(EXCLUDED) or not (root / name).is_file():
            continue
        data = (root / name).read_bytes()
        if b'\0' in data:
            continue
        try:
            before = data.decode('utf-8')
        except UnicodeDecodeError:
            continue
        after = rewrite(before, name, moves, root)
        if before != after:
            edits.append({'file': name, 'destination': moves.get(name, name),
                          'before': before, 'after': after})
        for line, content in enumerate(before.splitlines(), 1):
            for old in moves:
                parts = Path(old).parts
                if len(parts) > 1 and all(re.search(r'[\x27"]' + re.escape(p) + r'[\x27"]', content) for p in parts):
                    split_paths.append({'file': name, 'line': line, 'path': old, 'text': content.strip()})
    targets = [p for p in ('kapi/kapi_generated.c', 'kdebug/generated/kdebug_imports.h',
                           'hwdev/generated/hwdev_contract.h') if (root / p).is_file()]
    return edits, split_paths, targets


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('map', type=Path)
    ap.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()
    root = args.root.resolve()
    moves = load_map(root, args.map)
    edits, split_paths, targets = plan(root, moves)
    report = {'moves': moves, 'replacements': [dict(file=e['file'], destination=e['destination'],
               diff=list(difflib.unified_diff(e['before'].splitlines(), e['after'].splitlines(),
                                             e['file'], e['destination'], lineterm=''))) for e in edits],
              'split_paths': split_paths, 'regenerate': targets}
    print(json.dumps(report, indent=2), flush=True)
    if args.dry_run:
        return 0
    for old, new in moves.items():
        (root / new).parent.mkdir(parents=True, exist_ok=True)
        git(root, 'mv', '--', old, new)
    for edit in edits:
        (root / edit['destination']).write_text(edit['after'])
    if targets:
        subprocess.run(['make', *targets], cwd=root, check=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
