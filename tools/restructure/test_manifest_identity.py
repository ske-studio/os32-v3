"""Exercise the P2 gate through both command line entry points."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

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
    (tree / old_header).parent.mkdir()
    (tree / old_header).write_text('#define VALUE 17\n')
    def source(header, value='1'):
        return (f'#include "{header}"\nconst char path[] = "map: {header}";\n'
                f'int value = VALUE + {value};\n'
                'const char stamp[] = __DATE__ " " __TIME__ " " COMMIT;\n')
    (tree / old).write_text(source(old_header))
    (tree / blob).write_bytes(b'ISO')
    (tree / 'Makefile').write_text(
        'all: $(OBJ)\nbuild/out/obj/%.o: %.c\n\tmkdir -p $(@D)\n'
        '\t$(CC) -I. -DCOMMIT=\\"$(OS32_BUILD_COMMIT)\\" -c $< -o $@\n')
    def make(output):
        return ['make', 'OBJ=' + str(output)]
    def git(*args):
        subprocess.run(['git', '-c', 'user.name=test', '-c', 'user.email=test@localhost',
                        *args], cwd=tree, check=True, capture_output=True)
    git('init', '-q')
    git('add', 'Makefile', str(old), old_header)
    git('commit', '-qm', 'baseline')
    git('tag', '-a', 'v3-pre-restructure', '-m', 'capture baseline')
    subprocess.run([sys.executable, '-B', root / TOOLS[0], '--baseline-build', '--tree', tree,
                    '--', *make(old_obj)], check=True, stdout=subprocess.DEVNULL)
    sha, epoch = subprocess.check_output(['git', 'show', '-s', '--format=%H %ct'],
                                         cwd=tree, text=True).split()
    assert sha[:12].encode() in (tree / old_obj).read_bytes()
    stamp = time.strftime('%b %e %Y %H:%M:%S', time.gmtime(int(epoch))).encode()
    assert stamp in (tree / old_obj).read_bytes()
    manifest = directory / 'sha256sums'
    lines = []
    for path in (old_obj, blob):
        base = directory / path
        base.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(tree / path, base)
        lines.append(hashlib.sha256(base.read_bytes()).hexdigest() + '  ' + str(path))
    manifest.write_text('\n'.join(lines) + '\n')
    (directory / 'PRESENCE').write_text(str(blob) + '\n')
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
    library = Path('build/out/lib/libsample.a')
    member = tree / 'sample.o'
    subprocess.run(['cc', '-x', 'c', '-c', '-o', member, '-'],
                   input=b'const char sample[] = "abc";', check=True)
    (tree / library).parent.mkdir(parents=True)
    subprocess.run(['ar', 'rcD', tree / library, member], check=True)
    baseline = directory / library
    baseline.parent.mkdir(parents=True)
    shutil.copyfile(tree / library, baseline)
    original = baseline.read_bytes()
    with manifest.open('a') as output:
        output.write(hashlib.sha256(original).hexdigest() + '  ' + str(library) + '\n')
    changed_time = original[:24] + b'123'.ljust(12) + original[36:]
    (tree / library).write_bytes(changed_time)
    check('ar member time', 0, 'MATCH ar')
    # GNU ar stores its symbol index as the first member named "/".
    assert original[8:24].rstrip() == b'/'
    index_end = 68 + int(original[56:66])
    position = original.index(b'sample\0', 68, index_end)
    (tree / library).write_bytes(original[:position] + b'x' + original[position + 1:])
    check('ar symbol index', 1, 'DIFF ' + str(library))
    (tree / library).write_bytes(changed_time.replace(b'abc', b'abd'))
    check('ar member payload', 1, 'DIFF ' + str(library))
    draft = directory / 'declarations'
    digest = hashlib.sha256((tree / library).read_bytes()).hexdigest()
    draft.write_text(f'Sep-Changes: reviewed-replay {library} {digest} combined\n')
    replay = directory / 'replay' / library
    replay.parent.mkdir(parents=True)
    replay.write_bytes(original)
    args = ['--declarations', str(draft), '--replay-tree', str(directory / 'replay')]
    check('reviewed candidate and replay', 0, 'MATCH reviewed-replay', args=args)
    replay.write_bytes(changed_time)
    check('wrong replay bytes', 1, 'DIFF ' + str(library), args=args)
    replay.write_bytes(original)
    (tree / library).write_bytes(changed_time.replace(b'abc', b'abe'))
    check('unreviewed candidate bytes', 1, 'DIFF ' + str(library), args=args)
    (tree / library).write_bytes(original)
    (tree / new).parent.mkdir(parents=True)
    (tree / old).rename(tree / new)
    (tree / old_obj).unlink()
    (tree / blob).write_bytes(b'other ISO')
    check('moved', 0, '0 missing, 0 different', command=make(new_obj))
    # Change exactly one byte of the rebuilt object.
    path = tree / new_obj
    data = path.read_bytes()
    path.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
    check('one byte', 1, 'DIFF ' + str(new_obj))
    check('missing tool', 2, 'BUILD FAILED', command=['./no-such-tool'])
    (tree / new_obj).unlink()
    (tree / new).write_text('#error broken\n')
    check('broken make', 2, 'BUILD FAILED', command=make(new_obj))
    path.write_bytes(data)
    (tree / blob).unlink()
    check('missing ISO object', 3, 'MISSING ' + str(blob))
    (tree / blob).write_bytes(b'ISO')
    (tree / new_header).parent.mkdir(parents=True)
    (tree / old_header).rename(tree / new_header)
    (tree / new).write_text(source(new_header))
    git('add', '-u')
    git('add', str(new), new_header)
    git('commit', '-qm', f'Sep-Changes: path-string {old_obj} {old_header} {new}')
    path.unlink()
    check('recompiled header move', 0, 'MATCH path-string', command=make(new_obj))
    path.write_bytes(path.read_bytes() + b'x')
    check('extra byte after normalization', 1, 'DIFF ' + str(new_obj), args=['--base', 'HEAD^'])
    (tree / new).write_text(source(new_header, '2'))
    path.unlink()
    check('changed initializer', 1, 'DIFF ' + str(new_obj), command=make(new_obj))
    # Two moved names in a comment, including a repeated non-NUL occurrence.
    text_artifact = Path('public.h')
    def references(header, source):
        return (f'/*{header}*/ [{header}] "{header}" {source}\0'
                f'vendor/{old_header} {old_header}.bak')

    before = references(old_header, old)
    correct = references(new_header, new)
    broad = correct.replace(f'vendor/{old_header}', f'vendor/{new_header}')
    (directory / text_artifact).write_text(before)
    with manifest.open('a') as out:
        out.write(hashlib.sha256(before.encode()).hexdigest() + '  public.h\n')
    (tree / text_artifact).write_text(correct)
    asm = Path('lib/sample.asm')
    moved_asm = Path('sys/kern/sample.asm')
    asm_obj = Path('sample.o')
    for name in (asm, moved_asm):
        (tree / name).parent.mkdir(parents=True, exist_ok=True)
    (tree / asm).write_text('section .text\nglobal sample\nsample: ret\n')
    subprocess.run(['nasm', '-f', 'elf32', str(asm), '-o', str(asm_obj)], cwd=tree, check=True)
    shutil.copyfile(tree / asm_obj, directory / asm_obj)
    with manifest.open('a') as out:
        out.write(hashlib.sha256((tree / asm_obj).read_bytes()).hexdigest() + '  sample.o\n')
    (tree / asm).rename(tree / moved_asm)
    mapping.write_text(json.dumps(json.loads(mapping.read_text()) +
                                  [{'from': str(asm), 'to': str(moved_asm)}]))
    with (tree / 'Makefile').open('a') as out:
        out.write(f'AS = nasm\nsample.o: {moved_asm}\n\t$(AS) -f elf32 $< -o $@\n')
    # Restore the earlier control object before checking independent declarations.
    (tree / new).write_text(source(new_header))
    path.unlink()
    git('add', 'Makefile', str(moved_asm), str(new))
    git('commit', '-qm', f'Sep-Changes: path-string public.h {old_header}\n'
                         f'Sep-Changes: path-string public.h {old}\n'
                         f'Sep-Changes: path-string {asm_obj} {asm} {moved_asm}')
    (tree / asm_obj).unlink()
    media = 'boot-media.iso'
    with (directory / 'PRESENCE').open('a') as out:
        out.write(media + '\n')
    check('missing presence-only media', 3, 'MISSING ' + media, command=['make', str(new_obj), str(asm_obj)])
    (tree / media).touch()
    check('text and NASM', 0, '0 missing, 0 different', args=['--base', 'v3-pre-restructure'])
    (tree / text_artifact).write_text(broad)
    check('undeclared vendor path', 1, 'DIFF public.h', args=['--base', 'v3-pre-restructure'])
    (tree / text_artifact).write_text(correct)
    with (tree / text_artifact).open('a') as out:
        out.write(' ')
    check('unrelated text byte', 1, 'DIFF public.h', args=['--base', 'v3-pre-restructure'])
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
                    ('data[offset + 60:end]', 'len(payload)'),
                    ('        result.append((header[:16]', "        if header[:16].rstrip() == b'/':\n            offset = end\n            continue\n        result.append((header[:16]"),
                    ('missing += 1', 'missing += 0'),
                    ('for contents, expected in ((original, hashlib.sha256(data).hexdigest()),', 'for contents, expected in ((original, digest),'),
                    ('(normalized, digest)', '(original, digest)'),
                    ("hashes.keys() | presence", "hashes.keys()"),
                    ('pattern = PATH_START + b"(?:"', 'pattern = b"(?:"'),
                    ('+ b")" + PATH_END', '+ b")"'),
                    ('PATH_CHAR = rb"[A-Za-z0-9_./-]"', 'PATH_CHAR = rb"[A-Za-z0-9_./*-]"'),
                    ('PATH_CHAR = rb"[A-Za-z0-9_./-]"', 'PATH_CHAR = rb"[A-Za-z0-9_./\\[-]"'),
                    ("matches = re.sub(pattern,", "matches = re.sub(b'never-match',"),
                    ("(source, digest)):", "(moved, digest)):"),
                    ('SOURCE_DATE_EPOCH=epoch', "SOURCE_DATE_EPOCH='0'"),
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
