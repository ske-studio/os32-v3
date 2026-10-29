#!/usr/bin/env python3
"""Build two isolated revision snapshots and compare the shipped kernel images.

HEAD includes tracked edits and nonignored new files. Historical revisions are
unchanged except for the build-ID hook when they predate it; GCC's standard
SOURCE_DATE_EPOCH fixes __DATE__/__TIME__ in both trees. Keep logs / snapshots
under --output (default TMPDIR/build-compare-*); never write to either .git.
"""
import argparse
import difflib
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def git(root, *args):
    return subprocess.run(['git', *args], cwd=root, check=True,
                          capture_output=True).stdout


def snapshot(root, revision, destination):
    destination.mkdir()
    if revision == 'HEAD':
        # Copy the effective worktree directly. Starting from an archive would
        # retain the old name of a staged git mv (absent from ls-files).
        names = git(root, 'ls-files', '-z', '--cached', '--others', '--exclude-standard').decode().split('\0')
        for name in names:
            if not name or name == '.env' or name.startswith(('docs/archive/', 'docs/hw/')):
                continue
            source, target = root / name, destination / name
            if source.is_file():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
    else:
        data = git(root, 'archive', '--format=tar', revision)
        with tarfile.open(fileobj=io.BytesIO(data)) as archive:
            for member in archive:
                if member.name == '.env' or member.name.startswith(('docs/archive/', 'docs/hw/')):
                    continue
                archive.extract(member, destination, filter='data')
    # Historic L0 baseline lacks the environment override. Supply only this
    # hook, retaining its original make rules, flags and source code.
    generator = destination / 'tools/gen_build_id.py'
    if 'OS32_BUILD_COMMIT' not in generator.read_text():
        shutil.copy2(root / 'tools/gen_build_id.py', generator)
        print(f'{revision}: supplied build-ID comparison hook', flush=True)


def artifacts(root):
    out = root / 'build/out'
    paths = {Path(name) for name in ('kernel.bin', 'vmkernel.lz4', 'kdebug.bin', 'hwdev.mod',
                                    'r1/kernel.bin', 'r1/vmkernel.lz4', 'r1/kdebug.bin', 'r1/hwdev.mod')}
    paths.update(p.relative_to(out) for p in out.glob('*.mod'))
    paths.update(p.relative_to(out) for p in out.rglob('hwdev.bin'))
    paths.update(p.relative_to(out) for p in (out / 'r1').glob('*.mod'))
    return paths


def compare(left, right):
    paths = artifacts(left) | artifacts(right)
    different = []
    for rel in sorted(paths):
        a, b = left / 'build/out' / rel, right / 'build/out' / rel
        equal = a.is_file() and b.is_file() and a.read_bytes() == b.read_bytes()
        print(('MATCH ' if equal else 'DIFF  ') + str(rel), flush=True)
        if not equal:
            different.append(str(rel))
    return different


def functions(objdump, file):
    result = subprocess.run([objdump, '-drw', str(file)], check=True,
                            capture_output=True, text=True)
    result_map, name = {}, None
    for line in result.stdout.splitlines():
        label = re.match(r'^[0-9a-f]+ <(.+)>:$', line)
        if label:
            name = label[1]
            result_map[name] = []
        elif name and re.match(r'^\s*[0-9a-f]+:', line):
            result_map[name].append(re.sub(r'^\s*[0-9a-f]+:\s*', '', line))
    return result_map


def object_paths(root):
    paths = {}
    for directory in ('kernel', 'drivers', 'fs', 'gfx', 'exec', 'kapi', 'lib', 'net', 'platform', 'build/out'):
        for path in (root / directory).rglob('*.o'):
            key = path.relative_to(root).as_posix()
            if key.startswith('build/out/obj/'):
                key = key[len('build/out/obj/'):]
            paths[key] = path
    return paths


def function_diff(left, right, objdump):
    a, b = object_paths(left), object_paths(right)
    lines = []
    for key in sorted(a.keys() | b.keys()):
        if key not in a or key not in b:
            lines.append(f'{key}: object missing on one side\n')
            continue
        if a[key].read_bytes() == b[key].read_bytes():
            continue
        fa, fb = functions(objdump, a[key]), functions(objdump, b[key])
        for name in sorted(fa.keys() | fb.keys()):
            before, after = fa.get(name, []), fb.get(name, [])
            if before != after:
                lines.append(f'{key}: {name}\n')
                lines.extend(line + '\n' for line in difflib.unified_diff(before, after, 'base', 'target', lineterm=''))
    return ''.join(lines) or 'No function changes; inspect data sections and image metadata.\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('base')
    ap.add_argument('target')
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    output = args.output or Path(tempfile.mkdtemp(prefix='build-compare-'))
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, OS32_BUILD_COMMIT='layout-compare', SOURCE_DATE_EPOCH='946684800',
               TZ='UTC', LC_ALL='C', MUTATE='0')
    cross = Path(env.get('CROSS_DIR', '/home/hight/opt/cross'))
    env['PATH'] = str(cross / 'bin') + os.pathsep + env['PATH']
    print('comparison directory: ' + str(output), flush=True)
    trees = []
    for label, revision in (('base', args.base), ('target', args.target)):
        tree = output / label
        snapshot(ROOT, revision, tree)
        trees.append(tree)
        log = output / (label + '.log')
        with log.open('w') as stream:
            result = subprocess.run(['make', 'kernel', 'kernel-r1'], cwd=tree, env=env,
                                    stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            print(f'{label}: build failed, rc={result.returncode}: {log}', flush=True)
            return result.returncode
    different = compare(*trees)
    if different:
        report = output / 'function-diff.txt'
        report.write_text(function_diff(*trees, str(cross / 'bin/i386-elf-objdump')))
        print('function differences: ' + str(report))
        return 1
    print(f'All {len(artifacts(trees[0]))} artifacts match.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
