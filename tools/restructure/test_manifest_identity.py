"""Exercise the P2 gate through both command line entry points."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ['tools/build_compare.py', 'tools/ci/sep_identity.py']


def exercise(root, directory):
    tree = directory / 'candidate'
    tree.mkdir()
    old = Path('kernel/example.c')
    new = Path('sys/kern/example.c')
    obj = Path('build/out/obj')
    blob = Path('build/out/ppc-kboot/sh/root_image_blob.o')
    old_obj = obj / old.with_suffix('.o')
    new_obj = obj / new.with_suffix('.o')
    old_header = 'include/memmap.h'
    new_header = 'sys/include/memmap.h'
    for name in (old, blob, old_obj):
        (tree / name).parent.mkdir(parents=True, exist_ok=True)
    (tree / old).write_text('const char path[] = "' + old_header + '";\n')
    (tree / blob).write_bytes(b'ISO')
    (tree / 'Makefile').write_text('all:\n\tmkdir -p $(dir $(OBJ))\n\t$(CC) -c $(SRC) -o $(OBJ)\n')
    def make(source, output):
        return ['make', 'SRC=' + str(source), 'OBJ=' + str(output)]
    subprocess.run(make(old, old_obj), cwd=tree, check=True, stdout=subprocess.DEVNULL)
    manifest = directory / 'sha256sums'
    lines = []
    for path in (old_obj, blob):
        base = directory / path
        base.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(tree / path, base)
        lines.append(hashlib.sha256(base.read_bytes()).hexdigest() + '  ' + str(path))
    manifest.write_text('\n'.join(lines) + '\n')
    mapping = directory / 'map.json'
    mapping.write_text(json.dumps([{'from': str(old), 'to': str(new)},
                                   {'from': old_header, 'to': new_header}]))
    def check(label, rc, message, args=(), command=()):
        tool = TOOLS[1 if command else 0]
        result = subprocess.run([sys.executable, '-B', root / tool, '--manifest', manifest,
                                 '--tree', tree, '--move-map', mapping, *args,
                                 *(['--', *command] if command else [])],
                                capture_output=True, text=True)
        assert result.returncode == rc and message in result.stdout, (label, result)
        print(label, message)

    check('unchanged', 0, '0 missing, 0 different')
    (tree / new).parent.mkdir(parents=True)
    (tree / old).rename(tree / new)
    (tree / old_obj).unlink()
    (tree / blob).write_bytes(b'other ISO')
    check('moved', 0, '0 missing, 0 different', command=make(new, new_obj))
    # Change exactly one byte of the rebuilt object.
    path = tree / new_obj
    data = path.read_bytes()
    path.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
    check('one byte', 1, 'DIFF ' + str(new_obj))
    check('missing tool', 2, 'BUILD FAILED', command=['./no-such-tool'])
    (tree / new).write_text('#error broken\n')
    check('broken make', 2, 'BUILD FAILED', command=make(new, new_obj))
    path.write_bytes(data)
    (tree / blob).unlink()
    check('missing ISO object', 3, 'MISSING ' + str(blob))
    (tree / blob).write_bytes(b'ISO')
    def git(*args):
        subprocess.run(['git', '-c', 'user.name=test', '-c', 'user.email=test@localhost',
                        *args], cwd=tree, check=True, capture_output=True)
    git('init', '-q')
    git('commit', '--allow-empty', '-qm', 'baseline')
    git('commit', '--allow-empty', '-qm', f'Sep-Changes: path-string {old_obj} {old_header}')
    path.write_bytes(data.replace(old_header.encode(), new_header.encode()))
    check('declared string', 0, 'MATCH path-string', args=['--base', 'HEAD^'])
    path.write_bytes(path.read_bytes() + b'x')
    check('extra byte after normalization', 1, 'DIFF ' + str(new_obj), args=['--base', 'HEAD^'])
    git('commit', '--allow-empty', '-qm', 'Sep-Changes: x86')
    result = subprocess.run([sys.executable, '-B', root / TOOLS[0], '--manifest', manifest,
                             '--tree', tree, '--move-map', mapping, '--base', 'HEAD^'],
                            capture_output=True, text=True)
    assert result.returncode == 2 and 'expected Sep-Changes' in result.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        exercise(ROOT, work)
        if args.mutate:
            for i, (before, after) in enumerate([
                    ('                    return moved', '                    return old'),
                    ("if result.returncode:\n            print('BUILD FAILED'", "if False:\n            print('BUILD FAILED'"),
                    ('missing += 1', 'missing += 0'),
                    (' == data:', ' != data:'),
                    ('different += 1\n    if not seen', 'different += 0\n    if not seen')]):
                mutant = work / str(i)
                for file in TOOLS:
                    target = mutant / file
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(ROOT / file, target)
                target = mutant / TOOLS[0]
                source = target.read_text()
                assert source.count(before) == 1
                target.write_text(source.replace(before, after))
                try:
                    exercise(mutant, mutant)
                except AssertionError:
                    print('runtime RED', before.strip())
                else:
                    raise AssertionError('survived: ' + before)
    print('PASS manifest identity')


if __name__ == '__main__':
    main()
