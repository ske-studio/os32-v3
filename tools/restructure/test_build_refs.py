"""Exercise the inventory with an independent miniature build tree."""
from pathlib import Path
import csv
import subprocess
import sys
import tempfile

import build_refs


def table(root, name):
    with (root / name).open() as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def check():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / 'Makefile').write_text('include sdk/pkg/flags.mk\n')
        sdk = root / 'sdk/pkg'
        sdk.mkdir(parents=True)
        (sdk / 'flags.mk').write_text('FLAGS = -Ib \\\n -Ia\nobj: src.c\n')
        rows = list(build_refs.definitions(root))
        assert ('sdk/pkg/flags.mk', 1, 'include-order', 'FLAGS = -Ib -Ia') in rows
        assert any('dependency' in kind and value == 'obj: src.c' for _, _, kind, value in rows)
        log = 'MAKEFILE_LIST := Makefile sdk/pkg/flags.mk dep.d outside.mk'
        indexed = {path for path, _, kind, _ in rows if kind == 'file'}
        assert set(build_refs.observed(log)) - indexed == {'outside.mk'}
        (root / 'Makefile').write_text(
            'include sdk/pkg/flags.mk\ninclude outside.mk\n'
            'all: fixture.elf\nfixture.elf: src.c\n'
            '\tld $(FLAGS) second.o first.o -o $@\n')
        (root / 'outside.mk').touch()
        (root / 'src.c').touch()
        build_refs.ROOT = root
        assert build_refs.main(root)
        assert any(row['file'] == 'outside.mk' and row['covered'] == '0'
                   for row in table(root, 'read_mk.tsv'))
        assert any(row['directories_in_order'] == 'b a' and row['target'] == 'fixture.elf'
                   for row in table(root, 'include_order.tsv'))
        assert any(row['recipe_file'] == 'Makefile' and 'second.o first.o' in row['expanded_command']
                   for row in table(root, 'link_order.tsv'))



if __name__ == '__main__':
    check()
    if '--mutate' in sys.argv:
        source = Path(build_refs.__file__).read_text()
        for old, new in [("('build/**/*.mk', 'sdk/**/*.mk')", "('build/**/*.mk',)"), ("p.endswith('.mk')", "False"),
                         ("return bool(missing)", "return False")]:
            assert source.count(old) == 1
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp)
                (path / 'build_refs.py').write_text(source.replace(old, new))
                script = path / 'test_build_refs.py'
                script.write_text(Path(__file__).read_text())
                run = subprocess.run([sys.executable, '-B', script], capture_output=True)
                assert run.returncode and b'AssertionError' in run.stderr, run.stderr
                print('RED', old)
    print('PASS restructure-p0-2')
