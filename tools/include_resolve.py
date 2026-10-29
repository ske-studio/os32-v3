#!/usr/bin/env python3
"""Resolve literal quoted includes using make's ordered -I paths.

After make all: include_resolve.py [-o includes.json]. For small trees use
--root DIR --include-dir DIR (repeatable) instead of the make command database.
Records include inactive directives; this is a path audit, not preprocessing.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"\n]+)"', re.M)
SUFFIXES = {'.c', '.h', '.inc', '.S', '.cc', '.cpp'}


def tracked_files(root):
    result = subprocess.run(['git', 'ls-files', '-z', '--cached', '--others',
                             '--exclude-standard'], cwd=root, capture_output=True,
                            check=True)
    return sorted({p for p in result.stdout.decode().split('\0') if p
                   and not p.startswith(('docs/archive/', 'docs/hw/'))
                   and (root / p).is_file()})


def compile_paths(root):
    # Default BUILD_OUT is deliberate: before L0 the alternate output rules had
    # different -I flags. Compare the rules actually used by make all.
    from clang_ast.build import _segments, parse_compile_lines
    result = subprocess.run(['make', '--no-print-directory', '-n', '-B', 'all'],
                            cwd=root, capture_output=True, text=True, check=True)
    paths = {}
    for line in result.stdout.splitlines():
        if 'gcc' not in line:
            continue
        for command in _segments(line):
            units = parse_compile_lines(shlex.join(command))
            if not units:
                continue
            unit = units[0]
            output = command[command.index('-o') + 1]
            # Give the pre-L0 and post-L0 core object the same identity.
            output = output.removeprefix('build/out/obj/')
            includes = []
            it = iter(unit['argv'])
            for arg in it:
                if arg in ('-I', '-iquote', '-isystem'):
                    includes.append(next(it))
                elif arg.startswith('-I'):
                    includes.append(arg[2:])
            dirs = tuple((root / d).resolve() for d in includes)
            paths.setdefault((unit['src'], output), set()).add(dirs)
    return paths


def resolve(root, files, paths, fallback=()):
    root = Path(root).resolve()
    rows = set()
    visited = set()
    parsed = {}

    def relative(path):
        return os.path.relpath(path, root).replace(os.sep, '/')

    def visit(file, directories, unit):
        file = file.resolve()
        key = (file, directories, unit)
        if key in visited:
            return
        visited.add(key)
        if file not in parsed:
            text = file.read_text(errors='replace')
            # Keep physical line numbers while removing comments.
            text = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                          lambda m: re.sub(r'[^\n]', ' ', m[0])
                          if m[0].startswith('/') else m[0], text, flags=re.S)
            parsed[file] = [(m[1], text.count('\n', 0, m.start()) + 1)
                            for m in INCLUDE.finditer(text)]
        for spelling, line in parsed[file]:
            target = next((p.resolve() for d in (file.parent, *directories)
                           if (p := d / spelling).is_file()), None)
            rows.add((unit, relative(file), line, spelling, relative(target) if target else None))
            if target and target.is_relative_to(root):
                visit(target, directories, unit)

    for key, profiles in sorted(paths.items(), key=lambda item: str(item[0])):
        source, unit = key if isinstance(key, tuple) else (key, key)
        if (root / source).is_file():
            for directories in sorted(profiles):
                visit(root / source, directories, unit)
    # Also audit headers / assembly inputs not reached by a C translation unit.
    reached = {relative(file) for file, _, _ in visited}
    for name in sorted(files):
        if Path(name).suffix not in SUFFIXES or name in reached:
            continue
        profiles = paths.get(name)
        if not profiles:
            peers = [p for key, ps in paths.items()
                     if Path(key[0] if isinstance(key, tuple) else key).parent == Path(name).parent
                     for p in ps]
            profiles = set(peers) or {tuple((root / d).resolve() for d in fallback)}
        for directories in sorted(profiles):
            visit(root / name, directories, name)
    return [dict(zip(('unit', 'file', 'line', 'include', 'target'), row))
            for row in sorted(rows, key=lambda r: tuple(str(x) if x is not None else '' for x in r))]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument('-I', '--include-dir', action='append', default=[])
    ap.add_argument('-o', '--output', type=Path)
    args = ap.parse_args()
    root = args.root.resolve()
    files = tracked_files(root)
    paths = {} if args.include_dir else compile_paths(root)
    data = json.dumps(resolve(root, files, paths, args.include_dir), indent=2) + '\n'
    if args.output:
        args.output.write_text(data)
    else:
        print(data, end='')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
