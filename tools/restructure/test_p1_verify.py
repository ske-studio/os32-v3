"""Exercise the scope ledger contract using an independent miniature Git tree."""
import argparse
import contextlib
import csv
import io
from pathlib import Path
import subprocess
import tempfile
from types import ModuleType

import p1_verify as verify


def fixture(root):
    subprocess.run(['git', 'init', '-q', str(root)], check=True)
    for path in ('code.c', 'tools/checks/fixture_runner.py', 'tools/restructure/inventory.py',
                 'tools/checks/restructure_p0_987.py'):
        dest = root / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.touch()
    subprocess.run(['git', 'add', '.'], cwd=root, check=True)
    return subprocess.check_output(['git', 'write-tree'], cwd=root, text=True).strip()


def exercise(module, root, tree):
    module.ROOT = root
    table = root / 'scope.tsv'
    product = ['code.c', 'P1-1', '製品', '仮 D-3', '']
    runner = ['tools/checks/fixture_runner.py', 'P1-4', '実行の基盤', '登録を実行', '']
    footer = [verify.EXCLUDED, ['tools/checks/restructure_p0_987.py'],
              ['tools/restructure/inventory.py']]

    def check(rows, expected_code, messages):
        with table.open('w', newline='', encoding='utf-8') as stream:
            csv.writer(stream, delimiter='\t').writerows([verify.HEADER, *rows])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = module.verify_scope(table, tree)
        assert code == expected_code, output.getvalue()
        for message in messages:
            assert message in output.getvalue(), message

    check([product, runner, *footer], 0, ['A1\tcode.c', '差 0'])
    check([['other.c', *product[1:]], runner, *footer], 1, ['不足: code.c', '余分: other.c'])
    check([product, product, runner, *footer], 1, ['重複: code.c'])
    check([product, [runner[0], '', *runner[2:]], *footer], 1, ['必須欄が空'])
    check([product, [runner[0], '文書', '文書', *runner[3:]], *footer], 1, ['tools の種類が不正'])
    check([product, runner, *footer[:-1]], 1, ['母数外 不足: tools/restructure/inventory.py'])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = Path(verify.__file__).read_text()
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        tree = fixture(root)
        exercise(verify, root, tree)
        if args.mutate:
            for anchor, replacement in (
                ('expected - counts.keys()', 'set()'),
                ('counts.keys() - expected', 'set()'),
                ('if count > 1:', 'if False:'),
                ('if any(not value.strip() for value in row[:4]):', 'if False:'),
                ("compare('母数外', exclusions, omitted, issues)", 'pass'),
                ('for path, reason in pending:', 'for path, reason in []:'),
            ):
                assert source.count(anchor) == 1, anchor
                mutant = ModuleType('mutant')
                mutant.__file__ = verify.__file__
                exec(compile(source.replace(anchor, replacement), verify.__file__, 'exec'),
                     mutant.__dict__)
                try:
                    exercise(mutant, root, tree)
                except AssertionError:
                    print('RED', anchor)
                else:
                    raise AssertionError('surviving mutation: ' + anchor)
    print('OK: P1 scope ledger contract')


if __name__ == '__main__':
    main()
