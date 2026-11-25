"""Behaviour of the P0-1 inventory and its exception reports."""
from pathlib import Path
import subprocess
import sys
import tempfile

import owners


def example(tables_for):
    entries = [('arch/x86/example.c', '100644'), ('docs/example.txt', '100644'),
               ('include/pc98.h', '100644'), ('kernel/unused.c', '100644'), ('unused.rs', '100644'), ('module', '160000')]
    source = entries[0][0]
    tables = tables_for(entries, ({source: {'image'}}, {source: {'A', 'B'}},
                                  {source: {'usb', 'exec'}}))
    rows = tables['owners']
    assert [row[0] for row in rows] == [path for path, _ in entries]
    assert rows[0] == (source, 'source', 'arch/x86', 'image', 'A,B', 'arch/x86', 'member')
    assert rows[1][1:3] == ('document', 'document: docs/')
    examples = {'tools/x.py': 'development-test', 'a/tests/x.c': 'development-test',
                '.github/workflows/x.yml': 'development-test', 'a/README': 'document',
                'tools/x.md': 'document', 'sdk/include/x.h': 'public-interface',
                'sdk/kapi.json': 'public-interface'}
    cases = [(path, '100644') for path in examples]
    extra = tables_for(cases, ({}, {}, {}))['owners']
    assert all(row[1] == examples[row[0]] and row[2].startswith(row[1] + ': ') for row in extra)
    action = '.github/actions/custom/action.yml'
    action_tables = tables_for([(action, '100644')], ({}, {}, {}))
    assert action_tables['owners'][0][1:3] == ('development-test', 'development-test: .github/actions/')
    assert not action_tables['owner-empty']
    assert rows[2][2] == 'named/platform/pc98'
    assert rows[-1][1] == 'gitlink'
    assert tables['no-set'] == [rows[3]]
    assert tables['out-of-scope'] == [rows[4]]
    assert tables['owner-empty'] == [rows[i] for i in (3, 4, 5)]
    assert sum(int(row[-1]) for row in tables['owner-empty-reasons']) == len(tables['owner-empty'])
    assert tables['multi-feature'] == [(*rows[0], 'exec,usb')]


def make_membership(inventory):
    images, groups, features = inventory()
    assert images['userland/rust/hello_gfx/src/lib.rs'] == {'pc98/userland/tests/hello_gfx.elf'}
    assert 'pc98/boot/boot_fat144.bin' in images['boot/boot_fat.asm']
    assert 'pc98/build/out/kernel.elf' in images['exec/exec_abort.inc']
    assert 'pc98/boot/boot_fat144.bin' not in images['exec/exec_abort.inc']
    path = 'fs/ext2/ext2_dir.c'
    assert 'build/sources/common.mk:C_COMMON_PC98_PENDING' in groups[path]
    assert features[path] == {'ide'}
    assert 'build/programs.mk:SHELL_SRC' in groups['userland/shell/main.c']

    for stem in ('cpu_setup', 'decrementer', 'exception', 'pmap'):
        path = f'arch/ppc/{stem}.c'
        assert 'build/ppc.mk:MMU_C' in groups[path], path
        assert any(image.endswith('/PPCMMU.ELF') for image in images[path]), path
    path = 'userland/lib/rt/pkg.c'
    assert 'build/ppc-user.mk:PPC_INST_EXTRA' in groups[path]
    assert any(image.startswith('powermac/') and image.endswith('/cdinst.elf')
               for image in images[path])


def run(mutate):
    example(owners.tables_for)
    make_membership(owners.inventory)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([sys.executable, '-B', owners.__file__, tmp], check=True)
        paths = subprocess.check_output(['git', 'ls-files', '-z'], cwd=owners.ROOT)
        rows = [line.split('\t') for line in (Path(tmp) / 'owners.tsv').read_text().splitlines()]
        assert [row[0] for row in rows] == paths.decode().split('\0')[:-1]
        assert all(len(row) == 7 for row in rows)
        # The class, link image and Make owner come from independent parts of the build.
        assert any(owner == 'device' and 'pc98/' in images and groups
                   for _, _, owner, images, groups, _, _ in rows)
        assert any('powermac' in images and 'pc98' in images and 'C_COMMON_CORE' in groups
                   for _, _, _, images, groups, _, _ in rows)
        # Compare unambiguous source stems with actual linker inputs, including NASM.
        by_stem = {}
        for path, kind, _, images, _, _, _ in rows:
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
        for old, new in [("path == 'sdk/kapi.json'", "False"),
                         ("parts[0] == 'docs' or", "False or"),
                         ("path.startswith('.github/actions/')", "False"),
                         ("path.startswith('.github/workflows/')", "False"),
                         ('len(features) >= 2', 'len(features) > 2'),
                         ("elif path.split('/')[0] in KERNEL_ROOTS or image:", "elif True:"),
                         ("'CLI', 'EXEC', 'IDE'", "'PC98_PENDING', 'CLI', 'EXEC', 'IDE'"),
                         ("target.lower().endswith", "target.endswith"),
                         ("'.elf', '.bin', '.mod'", "'.elf', '.mod'"),
                         ("if item in manifests:", "if False:"),
                         ("images[os.path.normpath(item)].add(image)", "pass"),
                         ("name, value = assignment.groups()", "name, value = assignment.groups()\n                if name == 'PPC_INST_EXTRA': continue"),
                         ("for v in names))", "for v in names if v != 'SHELL_SRC'))"),
                         ("owner(path)", "owner('common')"),
                         ('for path, mode in entries:', 'for path, mode in entries[:-1]:')]:
            assert original.count(old) == 1
            scope = {'__file__': str(path), '__name__': 'mutant'}
            exec(compile(original.replace(old, new), str(path), 'exec'), scope)
            try:
                if old.startswith(("'CLI'", 'target.lower()', 'name, value', 'for v in names',
                                   "'.elf'", 'if item in manifests', 'images[os.path.normpath')):
                    make_membership(scope['inventory'])
                else:
                    example(scope['tables_for'])
            except AssertionError:
                print('RED', old)
            else:
                raise AssertionError('survived: ' + old)
    print('PASS restructure-p0-1')
    return 0


if __name__ == '__main__':
    sys.exit(run('--mutate' in sys.argv))
