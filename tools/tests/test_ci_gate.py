#!/usr/bin/env python3
"""tools/ci/gate.py: 道具の事前確認・結果の表・回っていない検査を失敗に数えること (検査は回さない)。"""
CHECK = dict(name='ci-gate-host', tier=1, needs=[], budget=5,
    covers=['tools/ci/gate.py', 'tools/tests/test_ci_gate.py', 'tools/checks/run.py', 'tools/check_time.py',
            '.github/actions/gate-setup/action.yml', '.github/workflows/sep.yml'],
    basis=['CI_GATE_DESIGN §2', 'CI_GATE_DESIGN §3'],
)
import argparse
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'tools/ci/gate.py'


def load(path):
    spec = importlib.util.spec_from_file_location('ci_gate_under_test', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def suite(tool):
    class GateTest(unittest.TestCase):
        def test_missing_tools(self):
            with tempfile.TemporaryDirectory(prefix='os32-ci-gate-') as directory:
                cross = Path(directory) / 'cross/bin'
                cross.mkdir(parents=True)
                compiler = cross / 'i386-elf-gcc'
                compiler.write_text('#!/bin/sh\n')
                compiler.chmod(0o755)
                env = dict(CROSS_DIR=str(cross.parent), PPC_CROSS_DIR=str(Path(directory) / 'none'))
                present = {'nasm', 'gcc-13', 'qemu-i386', 'genisoimage', 'e2fsck', 'cargo',
                           'i386-elf-gcc', 'powerpc-elf-gcc'}
                found = tool.missing_tools(env, which=lambda name: '/usr/bin/' + name if name in present else None)
            # クロスコンパイラは PATH ではなく <変数>/bin で探す。足りない物は全部挙がる。
            self.assertEqual([name for name, _ in found], ['powerpc-elf-gcc', 'qemu-system-ppc'])
            self.assertEqual([name for name, _ in tool.missing_tools({}, which=lambda name: '/x')],
                             ['i386-elf-gcc', 'powerpc-elf-gcc'])

        def test_parse_times(self):
            text = 'check-a\t1.500000\t0\ngarbage\ncheck-b\t0.250000\t2\ncheck-a\t3.000000\t1\n'
            self.assertEqual(tool.parse_times(text), {'check-a': (3.0, 1), 'check-b': (0.25, 2)})

        def test_skip_lines(self):
            text = 'PASS x\nSKIP: missing tool\n  SKIP indented is output of a fixture\nSKIPPED 3\nSKIP ppc\n'
            self.assertEqual(tool.skip_lines(text), ['SKIP: missing tool', 'SKIP ppc'])

        def test_classify_and_render(self):
            expected = ['check-a', 'check-b', 'check-c', 'check-d']
            rows = {'check-a': (2.0, 0), 'check-b': (30.0, 2), 'check-d': (1.0, 0), 'check-other': (9.0, 1)}
            logs = {'check-a': 'ok\n', 'check-b': ''.join('line %d\n' % i for i in range(100)),
                    'check-d': 'SKIP: no qemu\nPASS\n'}
            checks = tool.classify(expected, rows, logs)
            self.assertEqual([(c['name'], c['status'], c['rc'], c['skips']) for c in checks],
                             [('check-a', 'pass', 0, 0), ('check-b', 'fail', 2, 0),
                              ('check-c', 'missing', None, 0), ('check-d', 'pass', 0, 1)])
            lines, failed = tool.render('1/6', True, checks, logs, tail=3)
            text = '\n'.join(lines)
            self.assertTrue(failed)
            self.assertIn('| 4 | 2 | 1 | 1 | 1 | 33 秒 |', text)
            self.assertIn('- `check-b` (rc=2、30 秒)', text)
            # 落ちた検査のログは末尾だけ。通った検査のログは出さない。
            self.assertIn('line 97\nline 98\nline 99', text)
            self.assertNotIn('line 96', text)
            self.assertNotIn('ok\n', text)
            self.assertIn('- `check-c`', text.split('### 回っていない')[1].split('###')[0])
            self.assertIn('- `check-d` (1 行)', text)
            # 時間の表は遅い順。
            self.assertLess(text.index('| `check-b` | 30 |'), text.index('| `check-a` | 2 |'))

        def test_keep_failed_logs(self):
            with tempfile.TemporaryDirectory(prefix='os32-ci-gate-') as directory:
                out = Path(directory)
                logs = out / 'logs'
                logs.mkdir()
                (logs / 'check-a.log').write_text('passed\n')
                (logs / 'check-b.log').write_text('0123456789')
                (out / 'failed-logs').mkdir()
                (out / 'failed-logs/stale.log').write_text('old run')
                tool.keep_failed_logs(out, logs, [
                    dict(name='check-a', status='pass'), dict(name='check-b', status='fail'),
                    dict(name='check-c', status='fail'), dict(name='check-d', status='missing')], limit=4)
                self.assertEqual(sorted(p.name for p in (out / 'failed-logs').iterdir()), ['check-b.log'])
                self.assertEqual((out / 'failed-logs/check-b.log').read_text(), '6789')

        def test_render_verdict(self):
            def verdict(checks):
                return tool.render('0/1', False, checks, {})[1]
            passed = dict(name='check-a', status='pass', seconds=1.0, rc=0, skips=0)
            self.assertFalse(verdict([passed]))
            # SKIP の行は挙げるが落とさない (試験が自分の出力として印字する物と見分けられない)。
            self.assertFalse(verdict([dict(passed, skips=2)]))
            self.assertTrue(verdict([passed, dict(name='check-b', status='missing', seconds=0.0, rc=None, skips=0)]))
            self.assertTrue(verdict([passed, dict(name='check-b', status='fail', seconds=1.0, rc=1, skips=0)]))
            self.assertIn('変異なし', tool.render('0/1', False, [passed], {})[0][0])

    return unittest.defaultTestLoader.loadTestsFromTestCase(GateTest)


MUTANTS = [
    ("found = bool(base) and os.access(os.path.join(base, 'bin', name), os.X_OK)", "found = which(name) is not None"),
    ("        if not found:\n            missing.append((name, how))", "        if not found:\n            return [(name, how)]"),
    ("if len(parts) == 3:", "if len(parts) == 3 and parts[0] not in rows:"),
    ("re.match(r'SKIP\\b', line)", "'SKIP' in line"),
    ("status='missing', seconds=0.0", "status='pass', seconds=0.0"),
    ("status='pass' if rc == 0 else 'fail'", "status='pass'"),
    ("splitlines()[-tail:]", "splitlines()"),
    ("return lines, bool(by['fail'] or by['missing'])", "return lines, bool(by['fail'])"),
    ("skipped = [c for c in by['pass'] if c['skips']]", "skipped = []"),
    ("sorted(checks, key=lambda c: -c['seconds'])", "list(checks)"),
    ("if check['status'] == 'fail' and source.is_file():", "if source.is_file():"),
    ("(kept / source.name).write_bytes(data[-limit:])", "(kept / source.name).write_bytes(data)"),
    ("    if kept.exists():\n        shutil.rmtree(kept)\n    kept.mkdir(parents=True)", "    kept.mkdir(parents=True, exist_ok=True)"),
]


def mutant(case):
    old, new = case
    source = TOOL.read_text()
    if source.count(old) != 1:
        raise AssertionError('mutation point changed: ' + old)
    with tempfile.TemporaryDirectory(prefix='os32-ci-gate-mut-') as directory:
        path = Path(directory) / 'gate.py'
        path.write_text(source.replace(old, new))
        result = unittest.TextTestRunner(stream=io.StringIO()).run(suite(load(path)))
    if result.errors or not result.failures:
        raise AssertionError('not runtime RED: ' + old + ' -> ' + new)
    return 'runtime RED: ' + old.strip().splitlines()[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if not unittest.TextTestRunner().run(suite(load(TOOL))).wasSuccessful():
        return 1
    if args.mutate:
        for case in MUTANTS:
            print(mutant(case))
        print('ci gate mutants: %d/%d runtime RED' % (len(MUTANTS), len(MUTANTS)))
    return 0


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_ci_gate.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
