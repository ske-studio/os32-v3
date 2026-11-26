#!/usr/bin/env python3
"""文書の行番号つきの参照の検査 (tools/ops/check_doc_line_refs.py) を、偽の木で確かめる。"""
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
TOOL = ROOT / 'tools/ops/check_doc_line_refs.py'


def load(path):
    spec = importlib.util.spec_from_file_location('doc_line_refs_under_test', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def suite(tool):
    class LineRefTest(unittest.TestCase):
        def setUp(self):
            self.dir = tempfile.TemporaryDirectory(prefix='os32-doc-line-refs-')
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

        def test_count_only_repository_paths_with_directory(self):
            files = {'unit/alpha.c', 'unit/sub/beta.h', 'tool/gamma.py', 'top.md'}
            text = ('`unit/alpha.c:12` と unit/sub/beta.h:3-9、`O/unit/alpha.c:40` は数える。'
                    '外の手本 `outside/driver/delta.c:77`、名前だけの `alpha.c:5`、'
                    '行の無い `unit/alpha.c`、節の `unit/alpha.c` の `start()` は数えない。'
                    'URL http://host/unit/alpha.c:80 と、ディレクトリの無い top.md:3 も数えない。')
            self.assertEqual(tool.count(text, files), 3)
            # 木の記法は 1 文字だけ。2 文字以上の頭は別のパス。
            self.assertEqual(tool.count('`XY/unit/alpha.c:1`', files), 0)
            self.assertEqual(tool.count('`N/unit/alpha.c:1`', files), 1)

        def test_scan_skips_archive_and_non_markdown(self):
            self.put({'unit/alpha.c': 'int a;\n',
                      'note/guide.md': '`unit/alpha.c:1` `unit/alpha.c:2`\n',
                      'note/clean.md': '`unit/alpha.c` の start\n',
                      'docs/archive/old.md': '`unit/alpha.c:1`\n',
                      'docs/hw/mirror.md': '`unit/alpha.c:1`\n',
                      'note/code.txt': 'unit/alpha.c:1\n'})
            self.assertEqual(tool.scan(self.root), {'note/guide.md': 2})

        def test_compare_is_a_two_way_ratchet(self):
            self.assertEqual(tool.compare({'a.md': 2}, {'a.md': 2}), [])
            more = tool.compare({'a.md': 3, 'new.md': 1}, {'a.md': 2})
            self.assertEqual(len(more), 2)
            self.assertIn('a.md: 行番号つきの参照が 3 件 (基準 2)', more[0])
            self.assertIn('new.md: 行番号つきの参照が 1 件 (基準 0)', more[1])
            fewer = tool.compare({'a.md': 1}, {'a.md': 2, 'gone.md': 4})
            self.assertEqual(len(fewer), 2)
            self.assertIn('a.md: 1 件に減った (基準 2)', fewer[0])
            self.assertIn('gone.md: 0 件に減った (基準 4)', fewer[1])

        def test_main_fails_on_growth_and_baseline_round_trip(self):
            self.put({'unit/alpha.c': 'int a;\n', 'note/guide.md': '`unit/alpha.c:1`\n'})
            rc, text = self.run_tool()
            self.assertEqual(rc, 1)          # 基準が無い = 0 件が基準
            self.assertIn('note/guide.md: 行番号つきの参照が 1 件 (基準 0)', text)
            rc, _ = self.run_tool('--write-baseline')
            self.assertEqual(rc, 0)
            written = json.loads((self.root / tool.BASELINE).read_text(encoding='utf-8'))
            self.assertEqual(written, {'note/guide.md': 1})
            self.assertEqual(self.run_tool()[0], 0)
            self.put({'note/guide.md': '`unit/alpha.c:1` `unit/alpha.c:9`\n'})
            self.assertEqual(self.run_tool()[0], 1)
            self.put({'note/guide.md': '`unit/alpha.c` の start\n'})
            rc, text = self.run_tool()
            self.assertEqual(rc, 1)
            self.assertIn('0 件に減った (基準 1)', text)

    return unittest.defaultTestLoader.loadTestsFromTestCase(LineRefTest)


MUTANTS = [
    ("if path in files or strip_tree_prefix(path) in files:", "if True:"),
    ("if path in files or strip_tree_prefix(path) in files:", "if path in files:"),
    ("return tail if len(head) == 1 and head.isalpha() and tail else path", "return tail if tail else path"),
    ("if not name.endswith('.md') or name.startswith(EXCLUDED):", "if not name.endswith('.md'):"),
    ("if not name.endswith('.md') or name.startswith(EXCLUDED):", "if name.startswith(EXCLUDED):"),
    ("        if have > allowed:", "        if False:"),
    ("        elif have < allowed:", "        elif False:"),
    ("have, allowed = observed.get(name, 0), baseline.get(name, 0)", "have, allowed = observed.get(name, 0), baseline.get(name, observed.get(name, 0))"),
    ("    return 1 if problems else 0", "    return 0"),
    ("((?:[A-Za-z0-9_.\\-]+/)+[A-Za-z0-9_.\\-]+\\.", "((?:[A-Za-z0-9_.\\-]+/)*[A-Za-z0-9_.\\-]+\\."),
]


def mutant(case):
    old, new = case
    source = TOOL.read_text(encoding='utf-8')
    if source.count(old) != 1:
        raise AssertionError('mutation point changed: ' + old)
    with tempfile.TemporaryDirectory(prefix='os32-doc-line-refs-mut-') as tmp:
        path = Path(tmp) / 'check_doc_line_refs.py'
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
        print('doc line refs mutants: %d/%d runtime RED' % (len(MUTANTS), len(MUTANTS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
