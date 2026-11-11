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
import hashlib
import json
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


def archive_members(data):
    """Compare payload members, including duplicate names, in stored order."""
    if not data.startswith(b'!<arch>\n'):
        raise ValueError('invalid ar archive')
    result, strings, offset = [], b'', 8
    while offset < len(data):
        header = data[offset:offset + 60]
        if len(header) != 60 or header[58:] != b'`\n':
            raise ValueError('invalid ar member header')
        size = int(header[48:58])
        payload = data[offset + 60:offset + 60 + size]
        if len(payload) != size:
            raise ValueError('truncated ar member')
        name = header[:16].decode().strip()
        offset += 60 + size + (size & 1)
        if name == '//':
            strings = payload
            continue
        if name in ('/', '/SYM64/'):
            continue
        if name.startswith('#1/'):
            length = int(name[3:])
            name, payload = payload[:length].decode(), payload[length:]
        elif name.startswith('/') and name[1:].isdigit():
            start = int(name[1:])
            end = strings.find(b'/\n', start)
            if end < 0:
                raise ValueError('invalid ar string offset')
            name = strings[start:end].decode()
        else:
            name = name.rstrip('/')
        result.append((name, payload))
    return result


def artifacts(root, all_products=False):
    """Relative paths, with objects and ELF metadata included in the verdict."""
    directories = ('build/out',)
    if all_products:
        directories += ('boot', 'programs', 'userland', 'sdk', 'lib', 'images', 'build/sdk')
    suffixes = {'.o', '.elf', '.bin', '.mod', '.lz4', '.a'}
    paths = set()
    for directory in directories:
        for path in (root / directory).rglob('*'):
            rel = path.relative_to(root)
            if 'target' in rel.parts or not path.is_file():
                continue
            if path.suffix in suffixes or (all_products and
                    (directory in ('programs', 'images', 'build/sdk') or
                     str(rel).startswith('sdk/include/'))):
                paths.add(rel)
    # Missing required products must fail even when absent on both sides.
    paths.update(Path('build/out') / name for name in (
        'kernel.bin', 'kernel.elf', 'vmkernel.lz4', 'kdebug.bin',
        'r1/kernel.bin', 'r1/kernel.elf', 'r1/vmkernel.lz4', 'r1/kdebug.bin'))
    return paths


# Approved B1/B2/T1 SDK tool changes, pinned to both reviewed contents.
# A path alone never permits an arbitrary tool change or a missing target.
ALLOWED_TOOL_DIFFS = {
    'build/sdk/bin/link_guard.py': ((
        '319f4e3fd31f4c12181966246f732b6824ee99fd644a0d72eecfd7967c85f92e',
        'b81f4292bedf5052a642d9f5b4fa37cf2e5788eaf8ffdf2bfa9f9a2a55f415e4'),),
    # 第 5 段 S1: SDK の選択 (CRT・link script・Rust target) を profile に足した。
    'build/sdk/bin/target_profile.py': ((
        '4f6491076f6737fa2f8fa0a80216efe1ae20a68fd80e16496a26dcb399b2d9a6',
        '6835c50b1236b84ca6dec2ab6810d06bcb45eb4bc064548c31687561c536198a'),
        # PowerPC シェル S-B B05: USER の能力の分離、本線の承認 2026-11-11
        ('6835c50b1236b84ca6dec2ab6810d06bcb45eb4bc064548c31687561c536198a',
         'ed85b1dda69dd3c885035781ec3daa86be8b724bfc941e8253ee719cec8593a9'),),
    # 第 5 段 S2: Rust の wrapper が SDK の target を rustc の起動より前に確かめる。
    'build/sdk/bin/rustc_stamp.py': ((
        'd5e0f249c10662334532b964a3c167e5f21fb189677520dbd9463f17307ecee7',
        'c75b669a285ef524450d283991b3a37480e5c67d5417d2e197290e2a36ea2ef1'),),
    # 第 5 段 S2: profile を make から引く口を SDK に配る (新規)。
    'build/sdk/bin/target_profile_cli.py': ((
        None, '8e2601ba7327d684644837b1717b29f2240e7c94357b17ed7260d47128d6e8cf'),
        # PowerPC シェル S-B B06: 能力単位の選択、本線の承認 2026-11-11
        ('8e2601ba7327d684644837b1717b29f2240e7c94357b17ed7260d47128d6e8cf',
         'bef3123738a377de72c1ebd753d6014d76722cfb46183b874353c6bedaa02217'),),
    # 第 5 段 S2: 例の Makefile は CRT・link script・ELF emulation を profile から引く。
    'build/sdk/example/hello/Makefile': ((
        '9e35704167f6fbddaceec6f2d08a140a61a54ad3742ad6e27f3eaee4d0deceaf',
        '3d7cd5d25413c5280dc3fd0a97c3a9ad7463011bbadcc2b69625c99304e9acc4'),),
    # 第 5 段 S2: target の選択の節を足した。
    'build/sdk/README.md': ((
        'c5ee0362cbb6683e1377ca3b1a54754c084bad14ac33fd61509b4707e5d50637',
        'c8f6063748bc57dd6590455cb1c22dd70da2eb806602162084cca4ea7fc80788'),),
}


def allowed_tool_difference(rel, a, b):
    approved = ALLOWED_TOOL_DIFFS.get(str(rel))
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
    return approved is not None and (digest(a), digest(b)) in approved


# H2: explicit old/new object paths; final images remain byte comparisons.
H2_OBJECT_MOVES = {
    'kernel/v86/' + name + '.o': 'arch/x86/legacy/' + name + '.o'
    for name in ('v86', 'v86_mem', 'v86_module', 'v86_gcap', 'v86_entry')
}

# E1 preserves the shim's position in both normal and R1 links.
H2_OBJECT_MOVES['exec/ring3_ls_shim.o'] = 'arch/x86/kernel/ring3_ls_shim.o'


def h2_object_path(rel, root):
    for old, new in H2_OBJECT_MOVES.items():
        if str(rel).endswith(old):
            moved = Path(str(rel)[:-len(old)] + new)
            if (root / moved).is_file():
                return moved
    return rel


def object_execution_relocations(path):
    cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))
    records = []
    for flags in ('-drwz', '-r'):
        output = subprocess.check_output(
            [str(cross / 'bin/i386-elf-objdump'), flags, str(path)], text=True)
        # Only the objdump file heading contains the compared object's path.
        records.append('\n'.join(line for line in output.splitlines()
                                 if not line.startswith(str(path) + ':')))
    return tuple(records)



def compare(left, right, all_products=False):
    paths = artifacts(left, all_products) | artifacts(right, all_products)
    for old, new in H2_OBJECT_MOVES.items():
        paths = {Path(str(rel)[:-len(new)] + old)
                 if str(rel).endswith(new) else rel for rel in paths}
    different = []
    for rel in sorted(paths):
        left_rel, right_rel = h2_object_path(rel, left), h2_object_path(rel, right)
        a, b = left / left_rel, right / right_rel
        equal = a.is_file() and b.is_file()
        if equal:
            before, after = a.read_bytes(), b.read_bytes()
            equal = (archive_members(before) == archive_members(after)
                     if rel.suffix == '.a' else before == after)
        if not equal and a.is_file() and b.is_file() and left_rel != right_rel:
            equal = object_execution_relocations(a) == object_execution_relocations(b)
            print(('EXEC/RELOC MATCH ' if equal else 'EXEC/RELOC DIFF ') + str(rel), flush=True)
        if not equal and all_products and allowed_tool_difference(rel, a, b):
            print('ALLOW ' + str(rel), flush=True)
            before = a.read_text().splitlines(True) if a.is_file() else []
            print(''.join(difflib.unified_diff(before, b.read_text().splitlines(True),
                                             'base/' + str(rel), 'target/' + str(rel))), flush=True)
            continue
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
            # E: compare the moved adapter against its original translation unit.
            key = key.replace("arch/x86/legacy/v86_call.o", "exec/v86_call.o")
            key = key.replace("platform/pc98/bootinfo_check.o", "init/bootinfo_check.o")
            key = key.replace("platform/pc98/bios_snapshot.o", "kernel/v86/v86_snapshot.o")
            for old, new in H2_OBJECT_MOVES.items():
                if key.endswith(new):
                    key = key[:-len(new)] + old
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
    ap.add_argument('--all', action='store_true', dest='all_products')
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
    inputs = []
    for label, revision in (('base', args.base), ('target', args.target)):
        tree = output / ('input-' + label)
        snapshot(ROOT, revision, tree)
        inputs.append(tree)
    workspace = output / 'workspace'
    font_manifest = {}
    for path in (ROOT / 'assets/fonts').rglob('*'):
        if path.is_file():
            font_manifest[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    (output / 'fonts.json').write_text(json.dumps(font_manifest, indent=2) + '\n')
    for label, tree_input in zip(('base', 'target'), inputs):
        tree_input.rename(workspace)
        for name in font_manifest:
            target = workspace / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)
        log = output / (label + '.log')
        goals = ['all', 'kernel-r1'] if args.all_products else ['kernel', 'kernel-r1', 'libs']
        with log.open('w') as stream:
            result = subprocess.run(['make', 'CROSS_DIR=' + str(cross), *goals],
                                    cwd=workspace, env=env,
                                    stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            print(f'{label}: build failed, rc={result.returncode}: {log}', flush=True)
            return result.returncode
        tree = output / label
        workspace.rename(tree)
        trees.append(tree)
    different = compare(*trees, args.all_products)
    if different:
        report = output / 'function-diff.txt'
        report.write_text(function_diff(*trees, str(cross / 'bin/i386-elf-objdump')))
        print('function differences: ' + str(report))
        return 1
    print(f'All {len(artifacts(trees[0], args.all_products))} artifacts match.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
