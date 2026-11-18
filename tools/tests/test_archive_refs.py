#!/usr/bin/env python3
"""アーカイブを指す参照の検査 (tools/check_archive_refs.py) を、偽の木で確かめる。"""
import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'tools/check_archive_refs.py'


def load(path):
    spec = importlib.util.spec_from_file_location('archive_refs_under_test', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def suite(tool):
    class ArchiveRefTest(unittest.TestCase):
        def setUp(self):
            self.dir = tempfile.TemporaryDirectory(prefix='os32-archive-refs-')
            self.root = Path(self.dir.name)
            subprocess.check_call(['git', 'init', '-q'], cwd=self.root)

        def tearDown(self):
            self.dir.cleanup()

        def put(self, files):
            for name, text in files.items():
                path = self.root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding='utf-8')
            subprocess.check_call(['git', 'add', '-A'], cwd=self.root)

        def run_tool(self, *argv):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = tool.main(['--root', str(self.root), *argv])
            return rc, out.getvalue()

        def test_group_names_the_three_places(self):
            self.assertEqual(tool.group('tools/tests/alpha_tdd.md'), 'test-notes')
            self.assertEqual(tool.group('unit/alpha.c'), 'source')
            self.assertEqual(tool.group('unit/beta.inc'), 'source')
            self.assertEqual(tool.group('boot/gamma.asm'), 'source')
            self.assertEqual(tool.group('tools/sub/delta.py'), 'python')
            # 対象の外: 覚えでない文書、tools の外の Python、試験の置き場の外の覚え。
            self.assertIsNone(tool.group('tools/tests/alpha.md'))
            self.assertIsNone(tool.group('docs/guide_tdd.md'))
            self.assertIsNone(tool.group('unit/epsilon.py'))
            self.assertIsNone(tool.group('docs/note.md'))

        def test_count_only_existing_archive_documents(self):
            files = {'docs/archive/box/OLD.md', 'docs/archive/box/deep/DATA.json', 'docs/archive/README.md',
                     'docs/current/NEW.md'}
            text = ('票: `docs/archive/box/OLD.md` §2 と docs/archive/box/deep/DATA.json は数える。'
                    '文の終わりの docs/archive/box/OLD.md. も数える。'
                    '運用の docs/archive/README.md、作り物の docs/archive/box/ABSENT.md、'
                    '置き場だけの docs/archive/box/、現行の docs/current/NEW.md は数えない。')
            self.assertEqual(tool.count(text, files), 3)

        def test_scan_counts_per_file_in_the_groups(self):
            self.put({'docs/archive/box/OLD.md': 'old\n',
                      'tools/tests/alpha_tdd.md': '票: `docs/archive/box/OLD.md` と docs/archive/box/OLD.md\n',
                      'unit/alpha.c': '/* 票 docs/archive/box/OLD.md */\n',
                      'tools/beta.py': '"""票: docs/archive/box/OLD.md"""\n',
                      'tools/clean.py': '"""票: docs/current/NEW.md"""\n',
                      'docs/note.md': '`docs/archive/box/OLD.md`\n',
                      'unit/gamma.py': '# docs/archive/box/OLD.md\n'})
            self.assertEqual(tool.scan(self.root),
                             {'tools/tests/alpha_tdd.md': 2, 'unit/alpha.c': 1, 'tools/beta.py': 1})
            self.assertEqual(tool.totals(tool.scan(self.root)), {'test-notes': 2, 'source': 1, 'python': 1})

        def test_compare_is_a_two_way_ratchet(self):
            self.assertEqual(tool.compare({'a.c': 2}, {'a.c': 2}), [])
            more = tool.compare({'a.c': 3, 'new.c': 1}, {'a.c': 2})
            self.assertEqual(len(more), 2)
            self.assertIn('a.c: アーカイブの文書を指す参照が 3 件 (基準 2)', more[0])
            self.assertIn('new.c: アーカイブの文書を指す参照が 1 件 (基準 0)', more[1])
            fewer = tool.compare({'a.c': 1}, {'a.c': 2, 'gone.c': 4})
            self.assertEqual(len(fewer), 2)
            self.assertIn('a.c: 1 件に減った (基準 2)', fewer[0])
            self.assertIn('gone.c: 0 件に減った (基準 4)', fewer[1])

        def test_main_fails_on_growth_and_baseline_round_trip(self):
            self.put({'docs/archive/box/OLD.md': 'old\n', 'unit/alpha.c': '/* docs/archive/box/OLD.md */\n'})
            rc, text = self.run_tool()
            self.assertEqual(rc, 1)          # 基準が無い = 0 件が基準
            self.assertIn('unit/alpha.c: アーカイブの文書を指す参照が 1 件 (基準 0)', text)
            rc, _ = self.run_tool('--write-baseline')
            self.assertEqual(rc, 0)
            written = json.loads((self.root / tool.BASELINE).read_text(encoding='utf-8'))
            self.assertEqual(written, {'unit/alpha.c': 1})
            rc, text = self.run_tool()
            self.assertEqual(rc, 0)
            self.assertIn('ソースの註 1', text)
            self.put({'unit/alpha.c': '/* docs/archive/box/OLD.md docs/archive/box/OLD.md */\n'})
            self.assertEqual(self.run_tool()[0], 1)
            self.put({'unit/alpha.c': '/* docs/current/NEW.md */\n'})
            rc, text = self.run_tool()
            self.assertEqual(rc, 1)
            self.assertIn('0 件に減った (基準 1)', text)

    return unittest.defaultTestLoader.loadTestsFromTestCase(ArchiveRefTest)


MUTANTS = [
    ("        if path in files and path != CURRENT:", "        if path != CURRENT:"),
    ("        if path in files and path != CURRENT:", "        if path in files:"),
    ("    if name.startswith('tools/tests/') and name.endswith('_tdd.md'):", "    if name.endswith('_tdd.md'):"),
    ("    if name.startswith('tools/tests/') and name.endswith('_tdd.md'):", "    if name.startswith('tools/tests/') and name.endswith('.md'):"),
    ("    if name.endswith(SOURCE_EXTS):", "    if name.endswith(('.c', '.h')):"),
    ("    if name.startswith('tools/') and name.endswith('.py'):", "    if name.endswith('.py'):"),
    ("        if group(name) is None:\n            continue\n", ""),
    ("        if have > allowed:", "        if False:"),
    ("        elif have < allowed:", "        elif False:"),
    ("have, allowed = observed.get(name, 0), baseline.get(name, 0)", "have, allowed = observed.get(name, 0), baseline.get(name, observed.get(name, 0))"),
    ("    return 1 if problems else 0", "    return 0"),
    ("docs/archive/(?:[A-Za-z0-9_\\-]+/)*[A-Za-z0-9_.\\-]+", "docs/archive/[A-Za-z0-9_.\\-]+"),
]


def mutant(case):
    old, new = case
    source = TOOL.read_text(encoding='utf-8')
    if source.count(old) != 1:
        raise AssertionError('mutation point changed: ' + old)
    with tempfile.TemporaryDirectory(prefix='os32-archive-refs-mut-') as tmp:
        path = Path(tmp) / 'check_archive_refs.py'
        path.write_text(source.replace(old, new), encoding='utf-8')
        result = unittest.TextTestRunner(stream=io.StringIO()).run(suite(load(path)))
    if result.errors or not result.failures:
        raise AssertionError('not runtime RED: ' + old + ' -> ' + new)
    return 'runtime RED: ' + old


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if not unittest.TextTestRunner().run(suite(load(TOOL))).wasSuccessful():
        return 1
    if args.mutate:
        for case in MUTANTS:
            print(mutant(case))
        print('archive refs mutants: %d/%d runtime RED' % (len(MUTANTS), len(MUTANTS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
