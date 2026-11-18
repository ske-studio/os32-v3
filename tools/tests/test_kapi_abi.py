#!/usr/bin/env python3
"""KAPI の末尾追加の検査 (tools/check_kapi_abi.py) を、偽の木で確かめる。"""
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
TOOL = ROOT / 'tools/check_kapi_abi.py'


def load(path):
    spec = importlib.util.spec_from_file_location('kapi_abi_under_test', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def spec_of(api, fields=(('heap', 'u32'),), capacity=8):
    return {'version': 1, 'func_capacity': capacity,
            'data_fields': [{'name': n, 'type': t, 'comment': 'c'} for n, t in fields],
            'api': [{'name': n, 'ret': r, 'args': list(a), 'target': 'k_' + n} for n, r, a in api]}


ONE = [('put', 'void', ['u8 c']), ('get', 'int', [])]


def suite(tool):
    class KapiAbiTest(unittest.TestCase):
        def setUp(self):
            self.dir = tempfile.TemporaryDirectory(prefix='os32-kapi-abi-')
            self.root = Path(self.dir.name)
            self.git('init', '-q', '-b', 'main')
            self.git('config', 'user.email', 'test@example.invalid')
            self.git('config', 'user.name', 'test')
            self.commit(spec_of(ONE), 'base')
            self.git('update-ref', 'refs/remotes/origin/main', 'HEAD')

        def tearDown(self):
            self.dir.cleanup()

        def git(self, *args):
            subprocess.check_call(['git', *args], cwd=self.root, stdout=subprocess.DEVNULL)

        def commit(self, spec, message):
            path = self.root / tool.SPEC
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(spec), encoding='utf-8')
            self.git('add', '-A')
            self.git('commit', '-q', '-m', message)

        def run_tool(self):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = tool.main(['--root', str(self.root)])
            return rc, out.getvalue()

        def test_projection_ignores_kernel_side_names(self):
            a = spec_of(ONE)
            b = spec_of(ONE)
            b['api'][0]['target'] = 'other_kernel_function'
            b['api'][1]['body'] = 'return 0;'
            b['data_fields'][0]['comment'] = 'changed'
            self.assertEqual(tool.projection(a), tool.projection(b))

        def test_compare_accepts_append_and_reports_each_break(self):
            base = tool.projection(spec_of(ONE))
            self.assertEqual(tool.compare(base, tool.projection(spec_of(ONE + [('new', 'int', ['int a'])]))), [])
            self.assertEqual(tool.compare(base, tool.projection(spec_of(ONE, fields=(('heap', 'u32'), ('more', 'u32'))))), [])
            removed = tool.compare(base, tool.projection(spec_of(ONE[:1])))
            self.assertEqual(len(removed), 1)
            self.assertIn('口が減った (2 → 1)', removed[0])
            renamed = tool.compare(base, tool.projection(spec_of([('put', 'void', ['u8 c']), ('fetch', 'int', [])])))
            self.assertEqual(renamed, ['口 1 が変わった: get → fetch'])
            retyped = tool.compare(base, tool.projection(spec_of([('put', 'void', ['u16 c']), ('get', 'int', [])])))
            self.assertEqual(retyped, ['口 0 が変わった: put → 同じ名前で型・引数が違う'])
            reordered = tool.compare(base, tool.projection(spec_of(ONE[::-1])))
            self.assertEqual(len(reordered), 2)
            field = tool.compare(base, tool.projection(spec_of(ONE, fields=(('heap', 'u16'),))))
            self.assertEqual(field, ['データの欄 0 が変わった: heap → 同じ名前で型・引数が違う'])
            capacity = tool.compare(base, tool.projection(spec_of(ONE, capacity=9)))
            self.assertEqual(capacity, ['口の表の容量が変わった (8 → 9)'])

        def test_declared_break_needs_a_reason(self):
            self.assertEqual(tool.declared_break('title\n\nKAPI-Break: 利用者が居ないので作り直す\n'), '利用者が居ないので作り直す')
            self.assertIsNone(tool.declared_break('title\n\nKAPI-Break:\n'))
            self.assertIsNone(tool.declared_break('title\n\nthis mentions KAPI-Break: in a sentence\n'))

        def test_main_on_a_branch_and_on_main(self):
            self.git('checkout', '-q', '-b', 'work')
            self.commit(spec_of(ONE + [('new', 'int', [])]), 'append\n\nKAPI-Break: 壊していないのに書いた宣言')
            rc, text = self.run_tool()
            self.assertEqual(rc, 0)
            self.assertNotIn('宣言あり', text)   # 食い違いが無ければ、宣言は使われない
            self.git('checkout', '-q', '-b', 'work2', 'main')
            self.commit(spec_of([('put', 'void', ['u8 c']), ('fetch', 'int', [])]), 'rename')
            rc, text = self.run_tool()
            self.assertEqual(rc, 1)
            self.assertIn('口 1 が変わった: get → fetch', text)
            self.commit(spec_of([('put', 'void', ['u8 c']), ('fetch', 'int', []), ('x', 'int', [])]),
                        'declare\n\nKAPI-Break: 試験の理由')
            rc, text = self.run_tool()
            self.assertEqual(rc, 0)
            self.assertIn('宣言あり (KAPI-Break: 試験の理由)', text)
            # main そのものの上では、1 つ前のコミットと比べる。
            self.git('checkout', '-q', 'main')
            self.commit(spec_of(ONE[:1]), 'drop on main')
            self.git('update-ref', 'refs/remotes/origin/main', 'HEAD')
            rc, text = self.run_tool()
            self.assertEqual(rc, 1)
            self.assertIn('口が減った (2 → 1)', text)

        def test_main_without_history_is_an_environment_error(self):
            self.git('update-ref', '-d', 'refs/remotes/origin/main')
            rc, text = self.run_tool()
            self.assertEqual(rc, 2)
            self.assertIn('基点の sdk/kapi.json を読めない', text)

    return unittest.defaultTestLoader.loadTestsFromTestCase(KapiAbiTest)


MUTANTS = [
    ("[(a['name'], a['ret'], tuple(a['args'])) for a in spec['api']]", "[(a['name'], a['ret']) for a in spec['api']]"),
    ("[(a['name'], a['ret'], tuple(a['args'])) for a in spec['api']]", "[(a['name'], a['ret'], tuple(a['args']), a.get('target')) for a in spec['api']]"),
    ("[(f['name'], f['type']) for f in spec.get('data_fields', [])]", "[f['name'] for f in spec.get('data_fields', [])]"),
    ("        if len(new) < len(old):", "        if False:"),
    ("            if a != b:", "            if a[0] != b[0]:"),
    ("    if base['func_capacity'] != current['func_capacity']:", "    if False:"),
    ("match = re.fullmatch(r'KAPI-Break:\\s*(\\S.*)', line.strip())", "match = re.search(r'KAPI-Break:\\s*(\\S.*)', line.strip())"),
    ("match = re.fullmatch(r'KAPI-Break:\\s*(\\S.*)', line.strip())", "match = re.fullmatch(r'KAPI-Break:\\s*(.*)', line.strip())"),
    ("    return git(root, 'rev-parse', 'HEAD^') if base == head else base", "    return base"),
    ("    if problems and reason:", "    if reason:"),
    ("    return 1 if problems else 0", "    return 0"),
    ("        return 2", "        return 0"),
]


def mutant(case):
    old, new = case
    source = TOOL.read_text(encoding='utf-8')
    if source.count(old) != 1:
        raise AssertionError('mutation point changed: ' + old)
    with tempfile.TemporaryDirectory(prefix='os32-kapi-abi-mut-') as tmp:
        path = Path(tmp) / 'check_kapi_abi.py'
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
        print('kapi abi mutants: %d/%d runtime RED' % (len(MUTANTS), len(MUTANTS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
