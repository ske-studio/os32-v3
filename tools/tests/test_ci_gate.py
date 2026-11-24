#!/usr/bin/env python3
"""tools/ci/gate.py: 道具の事前確認・結果の表・回っていない検査を失敗に数えること (検査は回さない)。"""
CHECK = dict(name='ci-gate-host', tier=1, needs=[], budget=5,
    covers=['tools/ci/gate.py', 'tools/tests/test_ci_gate.py', 'tools/checks/run.py', 'tools/check_time.py',
            '.github/actions/gate-setup/action.yml', '.github/workflows/sep.yml', '.github/workflows/check.yml'],
    basis=['CI_GATE_DESIGN §2', 'CI_GATE_DESIGN §3'],
)
from contextlib import redirect_stdout
from unittest.mock import patch
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
                found = tool.missing_tools(env, which=lambda name: '/usr/bin/' + name if name in present else None,
                                           exists=lambda path: 'openbios' in path)
            # クロスコンパイラは PATH ではなく <変数>/bin で探す。足りない物は道具もデータも全部挙がる。
            self.assertEqual([name for name, _ in found],
                             ['powerpc-elf-gcc', 'qemu-system-ppc', '/usr/share/seabios/vgabios-stdvga.bin'])
            self.assertEqual([name for name, _ in tool.missing_tools({}, which=lambda name: '/x',
                                                                     exists=lambda path: True)],
                             ['i386-elf-gcc', 'powerpc-elf-gcc'])

        def test_static_preflight(self):
            with tempfile.TemporaryDirectory() as directory:
                summary = Path(directory) / 'summary'
                with (patch.dict(tool.os.environ, GITHUB_STEP_SUMMARY=str(summary)),
                      patch.object(tool.shutil, 'which', return_value=None),
                      patch.object(tool.importlib, 'import_module', side_effect=ImportError),
                      redirect_stdout(io.StringIO()) as output):
                    self.assertEqual(tool.main(['preflight', '--env', 'static']), 2)
                self.assertEqual(summary.read_text(), output.getvalue())
                for name in ('ccache', 'make', 'gcc', 'e2fsck', 'mke2fs', 'clang',
                             'clang.cindex', 'yaml', 'lz4.block'):
                    self.assertRegex(output.getvalue(), '- ' + name + ' — (apt|pip): ')

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

        def test_run_each_runs_every_check_in_order(self):
            # --env の束: 途中で落ちても残りを回し、落ちた本数を返す。順は一覧のまま。
            seen = []

            def timed(name):
                seen.append(name)
                return {'check-b': 2, 'check-d': 1}.get(name, 0)
            self.assertEqual(tool.run_each(['check-a', 'check-b', 'check-c', 'check-d'], timed), 2)
            self.assertEqual(seen, ['check-a', 'check-b', 'check-c', 'check-d'])
            self.assertEqual(tool.run_each([], timed), 0)
            self.assertEqual(tool.run_each(['check-a', 'check-c'], timed), 0)

        def test_keep_failed_logs(self):
            with tempfile.TemporaryDirectory(prefix='os32-ci-gate-') as directory:
                out = Path(directory)
                logs = out / 'logs'
                logs.mkdir()
                (logs / 'check-a.log').write_text('passed\n')
                (logs / 'check-b.log').write_text('0123456789')
                (out / 'failed-logs').mkdir()
                (out / 'failed-logs/stale.log').write_text('old run')
                kept = tool.reset_failed_logs(out)
                tool.keep_failed_log(kept, logs / 'check-a.log', 0, limit=4)
                tool.keep_failed_log(kept, logs / 'check-b.log', 1, limit=4)
                tool.keep_failed_log(kept, logs / 'check-c.log', 1, limit=4)
                self.assertEqual(sorted(p.name for p in (out / 'failed-logs').iterdir()), ['check-b.log'])
                self.assertEqual((out / 'failed-logs/check-b.log').read_text(), '6789')

        def test_keep_evidence(self):
            with tempfile.TemporaryDirectory(prefix='os32-ci-gate-') as directory:
                out = Path(directory)
                guest = out / 'work/normal-7455'
                (guest / 'agent-0').mkdir(parents=True)
                (guest / 'serial-a.log').write_text('0123456789')
                (guest / 'agent-0/screen-x.txt').write_text('screen')
                (guest / 'PPCKERN.ELF').write_bytes(b'x' * 64)
                old = out / 'old'
                old.mkdir()
                (old / 'serial-a.log').write_text('stale')
                log = out / 'check-ppc-usb.log'
                log.write_text('evidence=%s\nRuntimeError: deadline evidence=%s\n' % (old, guest))
                kept = out / 'kept'
                self.assertEqual(tool.keep_evidence(kept, log, limit=100, per_file=4), 8)
                target = kept / 'check-ppc-usb'
                self.assertEqual((target / 'serial-a.log').read_text(), '6789')
                self.assertEqual((target / 'agent-0/screen-x.txt').read_text(), 'reen')
                self.assertFalse((target / 'PPCKERN.ELF').exists())
                self.assertEqual(tool.keep_evidence(out / 'kept2', log, limit=5, per_file=4), 5)
                self.assertEqual(tool.keep_evidence(out / 'kept3', out / 'missing.log'), 0)
                log.write_text('no evidence line\n')
                self.assertEqual(tool.keep_evidence(out / 'kept4', log), 0)

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
    ("('ccache', 'apt: ccache'), ", ""),
    ("    return 2\n\n\ndef parse_times", "    return 0\n\n\ndef parse_times"),
    ("        if timed(name) != 0:\n            failed += 1", "        if timed(name) != 0:\n            return 1"),
    ("        if timed(name) != 0:\n            failed += 1", "        if timed(name) != 0:\n            failed = 1"),
    ("found = bool(base) and os.access(os.path.join(base, 'bin', name), os.X_OK)", "found = which(name) is not None"),
    ("        if not found:\n            missing.append((name, how))", "        if not found:\n            return [(name, how)]"),
    ("missing += [(path, how) for path, how in FILES if not exists(path)]", "pass"),
    ("if len(parts) == 3:", "if len(parts) == 3 and parts[0] not in rows:"),
    ("re.match(r'SKIP\\b', line)", "'SKIP' in line"),
    ("status='missing', seconds=0.0", "status='pass', seconds=0.0"),
    ("status='pass' if rc == 0 else 'fail'", "status='pass'"),
    ("splitlines()[-tail:]", "splitlines()"),
    ("return lines, bool(by['fail'] or by['missing'])", "return lines, bool(by['fail'])"),
    ("skipped = [c for c in by['pass'] if c['skips']]", "skipped = []"),
    ("sorted(checks, key=lambda c: -c['seconds'])", "list(checks)"),
    ("if rc and source.is_file():", "if source.is_file():"),
    ("handle.seek(max(0, size - limit))", "handle.seek(0)"),
    ("    if kept.exists():\n        shutil.rmtree(kept)\n    kept.mkdir(parents=True)", "    kept.mkdir(parents=True, exist_ok=True)"),
    ("base = Path(paths[-1]) if paths else None", "base = Path(paths[0]) if paths else None"),
    ("take = min(size, per_file, limit - total)", "take = min(size, limit - total)"),
    ("take = min(size, per_file, limit - total)", "take = min(size, per_file)"),
    ("if not source.is_file() or not EVIDENCE_NAMES.search(source.name):", "if not source.is_file():"),
    ("handle.seek(size - take)", "handle.seek(0)"),
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
