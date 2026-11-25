"""Behaviour of the P0-1 inventory and its exception reports."""
from pathlib import Path
import subprocess
import sys
import tempfile

import owners


def example(tables_for):
    entries = [('arch/x86/example.c', '100644'), ('docs/example.md', '100644'),
               ('include/pc98.h', '100644'), ('unused.rs', '100644'), ('module', '160000')]
    source = entries[0][0]
    tables = tables_for(entries, ({source: {'image'}}, {source: {'A', 'B'}},
                                  {source: {'usb', 'exec'}}))
    rows = tables['owners']
    assert [row[0] for row in rows] == [path for path, _ in entries]
    assert rows[0] == (source, 'source', 'arch/x86', 'image', 'A,B')
    assert rows[2][2] == 'named/platform/pc98'
    assert rows[-1][1] == 'gitlink'
    assert tables['no-set'] == [rows[3]]
    assert tables['owner-empty'] == []
    assert tables['multi-feature'] == [(*rows[0], 'exec,usb')]


def run(mutate):
    example(owners.tables_for)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([sys.executable, '-B', owners.__file__, tmp], check=True)
        paths = subprocess.check_output(['git', 'ls-files', '-z'], cwd=owners.ROOT)
        rows = [line.split('\t') for line in (Path(tmp) / 'owners.tsv').read_text().splitlines()]
        assert [row[0] for row in rows] == paths.decode().split('\0')[:-1]
        assert all(len(row) == 5 for row in rows)
        # The class, link image and Make owner come from independent parts of the build.
        assert any(owner == 'device' and 'pc98/' in images and groups
                   for _, _, owner, images, groups in rows)
        assert any('powermac' in images and 'pc98' in images and 'C_COMMON_CORE' in groups
                   for _, _, _, images, groups in rows)
        # Compare unambiguous source stems with actual linker inputs, including NASM.
        by_stem = {}
        for path, kind, _, images, _ in rows:
            if kind == 'source':
                by_stem.setdefault(str(Path(path).with_suffix('.o')), []).append((path, images))
        mapped = []
        for line in (owners.ROOT / 'build/out/kernel.map').read_text().splitlines():
            if line.startswith('LOAD build/out/obj/'):
                matches = by_stem.get(line.removeprefix('LOAD build/out/obj/'), [])
                if len(matches) == 1:
                    path, images = matches[0]
                    assert 'pc98' in images.split(','), path
                    mapped.append(path)
        assert mapped, 'no tracked kernel source resolved from linker map'
    if mutate:
        path = Path(owners.__file__)
        original = path.read_text()
        for old, new in [('len(features) >= 2', 'len(features) > 2'),
                         ("not row[4]", "bool(row[4])"),
                         ("owner(path)", "owner('common')"),
                         ('for path, mode in entries:', 'for path, mode in entries[:-1]:')]:
            assert original.count(old) == 1
            scope = {'__file__': str(path), '__name__': 'mutant'}
            exec(compile(original.replace(old, new), str(path), 'exec'), scope)
            try:
                example(scope['tables_for'])
            except AssertionError:
                print('RED', old)
            else:
                raise AssertionError('survived: ' + old)
    print('PASS restructure-p0-1')
    return 0


if __name__ == '__main__':
    sys.exit(run('--mutate' in sys.argv))
