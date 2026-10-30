#!/usr/bin/env python3
"""Timing regressions with two fake checks; mutations never touch the live tree."""
CHECK = dict(name='check-times-host', tier=1,
    covers=['tools/checks/run.py', 'tools/check_time.py', 'build/sdk.mk', 'sdk/kapi.json'],
    needs=[], budget=47.1)

import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
TIMER = (ROOT / 'tools/check_time.py').read_text()
SDK = (ROOT / 'build/sdk.mk').read_text()
RUNNER = (ROOT / 'tools/checks/run.py').read_text()


class CheckTimesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for directory in ('tools/checks', 'sdk', 'build'):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        self.write('tools/check_time.py', TIMER)
        self.write('build/sdk.mk', SDK)
        self.write('tools/checks/run.py', RUNNER)
        self.write('drivers/video/a.c', 'int a;\n')
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        self.write('sdk/kapi.json', '{"version": 1}')
        self.write('Makefile', 'BUILD_OUT = build/out\n'
                   'FAIL ?= 0\nPREP_FAIL ?= 0\nexport FAIL PREP_FAIL EMPTY\ninclude build/sdk.mk\n'
                   '.PHONY: shared\nshared:\n\t@echo prepared >> prepared\n\t@exit $(PREP_FAIL)\n')
        for name in ('a', 'b'):
            self.write(f'tools/checks/{name}.py',
                       f"CHECK = dict(name='{name}', covers=['drivers/video/a.c'], budget=1, needs=['make:shared'])\n"
                       'def run(ctx):\n    import os, subprocess, time\n'
                       + ('    time.sleep(0.03)\n    print("fake-a", flush=True)\n    return 0\n'
                          if name == 'a' else
                          '    print("fake-b", file=__import__("sys").stderr, flush=True)\n'
                          '    return int(os.environ.get("FAIL", "0"))\n'))
        self.write('tools/' + 'check_tree_unchanged.py', '')
        self.env = dict(os.environ)
        # 呼び手の CHECK_TIMES・CHECK_TIME_CHILD を引き継ぐと、偽の木の表の置き場や前提が変わる。
        for key in ('MAKEFLAGS', 'MFLAGS', 'MAKELEVEL', 'MAKEOVERRIDES', 'CHECK_TIMES', 'CHECK_TIME_CHILD', 'CC_FILES_JSON', 'CC_TIER',
                    'MUTATE', 'MUTATE_TARGETS', 'FAIL', 'PREP_FAIL', 'EMPTY',
                    'CHECK_NEEDS', 'CHECK_PAR_TARGETS', 'CHECK_NO_NEEDS'):
            self.env.pop(key, None)

    def write(self, path, text):
        dest = self.root / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text)

    def make(self, target, *args):
        return subprocess.run(['make', '--no-print-directory', '-j2', '-k', target,
                               'FILES=drivers/video/a.c', *args],
                              cwd=self.root, env=self.env, capture_output=True, text=True,
                              timeout=15)

    def rows(self, path):
        rows = [line.split('\t') for line in path.read_text().splitlines()]
        self.assertEqual(len(rows), 2, rows)
        self.assertTrue(all(len(row) == 3 for row in rows), rows)
        self.assertEqual({row[0] for row in rows}, {'check-a', 'check-b'})
        return {row[0]: (float(row[1]), int(row[2])) for row in rows}

    def test_make_parallel_and_changed(self):
        for target in ('check-par', 'check-changed', 'check-fast'):
            for fail in (0, 7):
                with self.subTest(target=target, fail=fail):
                    path = self.root / 'custom/times.tsv'
                    path.parent.mkdir(exist_ok=True)
                    path.write_text('stale\n')
                    start = time.monotonic()
                    result = self.make(target, f'CHECK_TIMES={path}', f'FAIL={fail}')
                    self.assertEqual(result.returncode, 2 if fail else 0,
                                     result.stdout + result.stderr)
                    rows = self.rows(path)
                    if target == 'check-changed':
                        self.assertIn('tier 2, 2 checks', result.stderr)
                    self.assertEqual(rows['check-a'][1], 0)
                    self.assertEqual(rows['check-b'][1], 2 if fail else 0)
                    self.assertGreaterEqual(rows['check-a'][0], 0.03)
                    self.assertLessEqual(rows['check-a'][0], time.monotonic() - start)
                    self.assertEqual(result.stdout.count('fake-a\n'), 1)
                    self.assertEqual(result.stderr.count('fake-b\n'), 1)
        result = self.make('check-par')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.rows(self.root / 'build/out/check_times.tsv')

    def test_prepare_once_direct_empty_and_failure(self):
        for target in ('check-par', 'check-changed', 'check-fast', 'check-a'):
            with self.subTest(target=target):
                prepared = self.root / 'prepared'
                prepared.unlink(missing_ok=True)
                result = self.make(target, 'MUTATE=0')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(prepared.read_text().splitlines(), ['prepared'])
                prepared.unlink()
                result = self.make(target, 'PREP_FAIL=7', 'MUTATE=0')
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn('fake-a', result.stdout)
                self.assertNotIn('fake-b', result.stderr)
        (self.root / 'prepared').unlink()
        result = self.make('check-changed', 'FILES=userland/cmds/untested.c')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((self.root / 'prepared').exists())
        self.assertEqual((self.root / 'build/out/check_times.tsv').read_text(), '')

    def test_direct_make_dry_run_does_not_execute_check(self):
        result = self.make('check-a', '-n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('--run check-a', result.stdout)
        self.assertNotIn('fake-a', result.stdout)
        self.assertFalse((self.root / 'prepared').exists())
        normal = self.make('check-a', 'MAKEFLAGS= -- RUNNER_NAME=normal')
        self.assertEqual(normal.returncode, 0, normal.stdout + normal.stderr)
        self.assertIn('fake-a', normal.stdout)

    def test_listing_failure_stops_make(self):
        self.write('tools/checks/a.py', 'CHECK = dict(name="a", covers=[], budget=False)\ndef run(ctx): return 0\n')
        result = self.make('check-par')
        self.assertEqual(result.returncode, 2)
        self.assertIn('--list failed', result.stderr)
        self.assertFalse((self.root / 'prepared').exists())

    def test_command_rc_and_output(self):
        path = self.root / 'direct.tsv'
        result = subprocess.run([sys.executable, str(self.root / 'tools/check_time.py'),
                                 str(path), 'direct', sys.executable, '-c',
                                 'import sys; print("out"); print("err", file=sys.stderr); sys.exit(7)'],
                                capture_output=True, text=True)
        self.assertEqual((result.returncode, result.stdout, result.stderr), (7, 'out\n', 'err\n'))
        name, elapsed, rc = path.read_text().strip().split('\t')
        self.assertEqual((name, rc), ('direct', '7'))
        self.assertGreater(float(elapsed), 0)


def main():
    global TIMER, SDK
    originals = TIMER, SDK
    def run():
        return unittest.TextTestRunner().run(
            unittest.defaultTestLoader.loadTestsFromTestCase(CheckTimesTest)).wasSuccessful()
    if not run():
        return 1
    if '--mutate' in sys.argv:
        mutations = (
            ('timer', 'os.O_APPEND', 'os.O_TRUNC'),
            ('timer', '{rc}\\n', '{0}\\n'),
            ('timer', '{name}\\t', 'wrong\\t'),
            ('timer', 'elapsed = time.monotonic() - start', 'elapsed = 0'),
            ('sdk', '--reset "$(CHECK_TIMES)"', '--reset "$(CHECK_TIMES).unused"'),
            ('sdk', 'check-par CHECK_PAR_TARGETS="$$CC_STAGE1"', '$$CC_STAGE1'),
        )
        for index, (kind, old, new) in enumerate(mutations, 1):
            TIMER, SDK = originals
            source = TIMER if kind == 'timer' else SDK
            assert source.count(old) == 1, old
            if kind == 'timer':
                TIMER = source.replace(old, new)
            else:
                SDK = source.replace(old, new)
            with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                killed = not run()
            print(f'MUTATION {index} {"RED" if killed else "GREEN"}')
            if not killed:
                return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_check_times.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
