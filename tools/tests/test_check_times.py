#!/usr/bin/env python3
"""Timing regressions with two fake checks; mutations never touch the live tree."""
CHECK = dict(name='check-times-host', tier=1,
    covers=['tools/check_time.py', 'build/sdk.mk', 'sdk/kapi.json', 'build/checks.d/check-check-times-host.mk', 'tools/check_map.d/check-check-times-host.yaml'],
    needs=[], budget=3.75216)

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


class CheckTimesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for directory in ('tools', 'sdk', 'build/checks.d'):
            (self.root / directory).mkdir(parents=True)
        self.write('tools/check_time.py', TIMER)
        self.write('build/sdk.mk', SDK)
        self.write('sdk/kapi.json', '{"version": 1}')
        self.write('Makefile', 'BUILD_OUT = build/out\ninclude build/sdk.mk\n')
        self.write('build/checks.d/fake.mk',
                   'CHECK_PAR_ORDER += 001:check-a 002:check-b\n'
                   'check-a:\n\t@echo fake-a; sleep 0.03\n'
                   'check-b:\n\t@echo fake-b >&2; exit $(FAIL)\n'
                   '.PHONY: check-a check-b\nFAIL ?= 0\n')
        # These generated stubs are fixture inputs, not copies of the real tools.
        stubs = {'check_tree_unchanged': '',
                 'check_select': 'print(' + repr('CC_STAGE1="check-a check-b"\n'
                                                'CC_MUT1="check-b"\nCC_SUMMARY=fixture') + ')\n'}
        for name, source in stubs.items():
            self.write(f'tools/{name}.py', source)
        self.env = dict(os.environ)
        # 呼び手の CHECK_TIMES・CHECK_TIME_CHILD を引き継ぐと、偽の木の表の置き場や前提が変わる。
        for key in ('MAKEFLAGS', 'MFLAGS', 'MAKELEVEL', 'MAKEOVERRIDES', 'CHECK_TIMES', 'CHECK_TIME_CHILD'):
            self.env.pop(key, None)

    def write(self, path, text):
        (self.root / path).write_text(text)

    def make(self, target, *args):
        return subprocess.run(['make', '--no-print-directory', '-j2', '-k', target, *args],
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
                    self.assertEqual(rows['check-a'][1], 0)
                    self.assertEqual(rows['check-b'][1], 2 if fail else 0)
                    self.assertGreaterEqual(rows['check-a'][0], 0.03)
                    self.assertLessEqual(rows['check-a'][0], time.monotonic() - start)
                    self.assertEqual(result.stdout.count('fake-a\n'), 1)
                    self.assertEqual(result.stderr.count('fake-b\n'), 1)
        result = self.make('check-par')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.rows(self.root / 'build/out/check_times.tsv')

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
