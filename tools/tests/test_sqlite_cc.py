#!/usr/bin/env python3
"""Optional SQLite compiler dispatch: fallback, opt-out and failure propagation."""
CHECK = dict(name='sqlite-cc-host', tier=1, needs=[], budget=1,
    covers=['tools/tests/sqlite_cc.py', 'tools/tests/test_sqlite_cc.py',
            'build/config.mk', 'build/sqlite-proto.mk', 'build/programs.mk',
            '.github/workflows/build.yml', '.github/workflows/check.yml'])
import os
from pathlib import Path
import subprocess
import sys
import types
import unittest
from unittest.mock import patch
import sqlite_cc



class Dispatch(unittest.TestCase):
    def call(self, cache, disabled):
        args = ['gcc', '-c', 'sqlite3.c', '-o', 'sqlite.o']
        original = args.copy()
        result = subprocess.CompletedProcess(args, 7)
        with patch.dict(os.environ, {'OS32_NO_CCACHE': disabled}), \
                patch.object(sqlite_cc.shutil, 'which', return_value=cache), \
                patch.object(sqlite_cc.subprocess, 'run', return_value=result) as run:
            self.assertIs(sqlite_cc.build(args, check=True, cwd='/work'), result)
        self.assertEqual(args, original)
        self.assertEqual(run.call_args.kwargs, dict(check=True, cwd='/work'))
        return run.call_args.args[0], args

    def test_enabled(self):
        got, args = self.call('/usr/bin/ccache', '')
        self.assertEqual(got, ['/usr/bin/ccache', 'compiler_check=content', 'base_dir=/work', *args])

    def test_missing(self):
        got, args = self.call(None, '')
        self.assertEqual(got, args)

    def test_disabled(self):
        got, args = self.call('/usr/bin/ccache', '1')
        self.assertEqual(got, args)

    def test_failure(self):
        error = subprocess.CalledProcessError(2, ['gcc'])
        with patch.object(sqlite_cc.subprocess, 'run', side_effect=error):
            with self.assertRaises(subprocess.CalledProcessError):
                sqlite_cc.build(['gcc', '-c', 'sqlite3.c'], check=True)


def run(ctx):
    argv = [sys.executable, '-B', 'tools/tests/test_sqlite_cc.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)


if __name__ == '__main__':
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(Dispatch))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if '--mutate' in sys.argv:
        source = Path(sqlite_cc.__file__).read_text()
        anchor = "os.environ.get('OS32_NO_CCACHE') != '1'"
        assert source.count(anchor) == 1
        mutant = types.ModuleType('sqlite_cc_mutant')
        exec(compile(source.replace(anchor, 'True'), str(Path(sqlite_cc.__file__)), 'exec'), mutant.__dict__)
        sqlite_cc = mutant
        result = unittest.TestResult()
        Dispatch('test_disabled').run(result)
        assert len(result.failures) == 1 and not result.errors
        print('RED runtime: ignored OS32_NO_CCACHE')
