#!/usr/bin/env python3
"""CI の検査の所要の棚卸し (tools/ci/time_inventory.py) を、偽の記録で確かめる。"""
CHECK = dict(name='ci-time-inventory-host', tier=1, needs=[], budget=5,
    covers=['tools/ci/time_inventory.py', 'tools/tests/test_ci_time_inventory.py', 'tools/checks/weights.json'],
    basis=['docs/design/tools/ci/time_inventory.md §2', 'docs/design/tools/ci/time_inventory.md §3'],
)
import argparse
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'tools/ci/time_inventory.py'


def load(path):
    spec = importlib.util.spec_from_file_location('time_inventory_under_test', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def suite(tool):
    class InventoryTest(unittest.TestCase):
        def setUp(self):
            self.dir = tempfile.TemporaryDirectory(prefix='os32-ci-inventory-')
            self.root = Path(self.dir.name)

        def tearDown(self):
            self.dir.cleanup()

        def put(self, artifact, text):
            path = self.root / artifact / 'check_times.tsv'
            path.parent.mkdir(parents=True)
            path.write_text(text)
            return path

        def test_group_names_the_guest_job(self):
            self.assertEqual(tool.group('check-times-abc-ppc-qemu-2'), 'qemu-ppc')
            self.assertEqual(tool.group('check-times-abc-2'), 'x86')

        def test_load_keeps_successful_rows_only(self):
            one = self.put('check-times-abc-0', 'check-a\t2.5\t0\ncheck-b\t90\t1\nnoise\ncheck-c\t4\t0\nother\t1\t0\n')
            self.assertEqual(tool.load([one, one]), [('check-times-abc-0', {'a': 2.5, 'c': 4.0})])

        def test_shard_sums_and_imbalance(self):
            artifacts = [('check-times-abc-0', {'a': 60.0, 'b': 40.0}), ('check-times-abc-1', {'c': 50.0}),
                         ('check-times-abc-ppc-qemu-0', {'g': 300.0})]
            sums = tool.shard_sums(artifacts)
            self.assertEqual(sums['x86'], [('check-times-abc-0', 2, 100.0), ('check-times-abc-1', 1, 50.0)])
            self.assertEqual(sums['qemu-ppc'], [('check-times-abc-ppc-qemu-0', 1, 300.0)])
            self.assertEqual(tool.imbalance(sums['x86']), 2.0)
            self.assertIsNone(tool.imbalance(sums['qemu-ppc']))            # 1 本では比べられない
            self.assertIsNone(tool.imbalance([('x', 0, 0.0), ('y', 1, 5.0)]))

        def test_medians_take_guest_checks_from_the_guest_job_only(self):
            artifacts = [('check-times-abc-0', {'g': 1.0, 'h': 10.0}), ('check-times-abc-1', {'h': 30.0}),
                         ('check-times-abc-ppc-qemu-0', {'g': 600.0, 'h': 2.0}),
                         ('check-times-def-ppc-qemu-0', {'g': 800.0})]
            envs = {'g': ('ppc-cross', 'qemu-ppc'), 'h': ()}
            self.assertEqual(tool.medians(artifacts, envs), {'g': 700.0, 'h': 20.0})

        def test_drift_lists_heavy_mismatches_largest_first(self):
            measured = {'grew': 400.0, 'shrank': 20.0, 'same': 100.0, 'new': 90.0, 'tiny': 5.0, 'tinynew': 3.0}
            weights = {'grew': 50.0, 'shrank': 200.0, 'same': 120.0, 'tiny': 1.0}
            rows = tool.drift(measured, weights, 2.0)
            self.assertEqual(rows, [('new', 90.0, None), ('grew', 400.0, 50.0), ('shrank', 20.0, 200.0)])
            # ちょうど factor 倍は「ずれた」に入る。少し足りなければ入らない。
            self.assertEqual(tool.drift({'x': 100.0}, {'x': 50.0}, 2.0), [('x', 100.0, 50.0)])
            self.assertEqual(tool.drift({'x': 99.0}, {'x': 50.0}, 2.0), [])

        def test_render_verdict(self):
            balanced = {'x86': [('s0', 2, 100.0), ('s1', 2, 90.0)]}
            lines, action = tool.render(balanced, {'a': 100.0, 'b': 90.0}, [], 1.5)
            self.assertFalse(action)
            self.assertIn('偏り (最大 ÷ 最小): 1.11', '\n'.join(lines))
            self.assertEqual(lines[-1], '判定: 手当ては要らない')
            skewed = {'x86': [('s0', 2, 100.0), ('s1', 2, 40.0)]}
            lines, action = tool.render(skewed, {'a': 100.0, 'b': 40.0}, [], 1.5)
            self.assertTrue(action)
            self.assertIn('閾値 1.50 を超えた', '\n'.join(lines))
            lines, action = tool.render(balanced, {'a': 100.0, 'b': 90.0}, [('a', 100.0, 10.0), ('n', 50.0, None)], 1.5)
            self.assertTrue(action)
            text = '\n'.join(lines)
            self.assertIn('| `a` | 100 | 10 |', text)
            self.assertIn('| `n` | 50 | 無い |', text)
            self.assertTrue(lines[-1].startswith('判定: 重みを作り直す'))
            # 重い順の表は上位だけ。
            many = {'c%02d' % i: float(100 - i) for i in range(30)}
            lines, _ = tool.render(balanced, many, [], 1.5, top=3)
            text = '\n'.join(lines)
            self.assertIn('| `c00` | 100 |', text)
            self.assertIn('| `c02` | 98 |', text)
            self.assertNotIn('`c03`', text)

        def test_main_without_records_is_an_environment_error(self):
            empty = self.put('check-times-abc-0', '')
            out = io.StringIO()
            stdout, sys.stdout = sys.stdout, out
            try:
                rc = tool.main([str(empty)])
            finally:
                sys.stdout = stdout
            self.assertEqual(rc, 2)

    return unittest.defaultTestLoader.loadTestsFromTestCase(InventoryTest)


MUTANTS = [
    ("return 'qemu-ppc' if QEMU_ARTIFACT in artifact else 'x86'", "return 'x86'"),
    ("and int(parts[2]) == 0:", "and True:"),
    ("if len(parts) == 3 and parts[0].startswith('check-')", "if len(parts) == 3"),
    ("    for path in sorted(set(paths)):", "    for path in sorted(paths):"),
    ("    if len(sums) < 2 or min(sums) <= 0:", "    if min(sums) <= 0:"),
    ("    return max(sums) / min(sums)", "    return min(sums) / max(sums)"),
    ("if ('qemu-ppc' in envs.get(check, ())) == (group(name) == 'qemu-ppc'):", "if True:"),
    ("        if seconds < floor and (weight is None or weight < floor):\n            continue\n", ""),
    ("        elif seconds >= weight * factor or seconds * factor <= weight:", "        elif seconds > weight * factor or seconds * factor <= weight:"),
    ("        elif seconds >= weight * factor or seconds * factor <= weight:", "        elif seconds >= weight * factor:"),
    ("            out.append((check, seconds, None))", "            pass"),
    ("            over = ratio > max_imbalance", "            over = False"),
    ("    if drifted:\n        action = True", "    if drifted:\n        action = action"),
    ("[:top]\n    total = sum(measured.values())", "\n    total = sum(measured.values())"),
    ("        print('check_times.tsv の記録が無い')\n        return 2", "        return 0"),
]


def mutant(case):
    old, new = case
    source = TOOL.read_text(encoding='utf-8')
    if source.count(old) != 1:
        raise AssertionError('mutation point changed: ' + old)
    with tempfile.TemporaryDirectory(prefix='os32-ci-inventory-mut-') as tmp:
        path = Path(tmp) / 'time_inventory.py'
        path.write_text(source.replace(old, new), encoding='utf-8')
        result = unittest.TextTestRunner(stream=io.StringIO()).run(suite(load(path)))
    if result.errors or not result.failures:
        raise AssertionError('not runtime RED: ' + old + ' -> ' + new)
    return 'runtime RED: ' + old.split('\n')[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if not unittest.TextTestRunner().run(suite(load(TOOL))).wasSuccessful():
        return 1
    if args.mutate:
        for case in MUTANTS:
            print(mutant(case))
        print('ci time inventory mutants: %d/%d runtime RED' % (len(MUTANTS), len(MUTANTS)))
    return 0


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_ci_time_inventory.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
