#!/usr/bin/env python3
"""Optional SQLite compiler dispatch: fallback, opt-out and failure propagation."""
CHECK = dict(name='sqlite-cc-host', tier=1, needs=[], budget=1,
    covers=['tools/tests/cc_cache.py', 'tools/tests/test_cc_cache.py',
            'build/config.mk', 'build/sqlite-proto.mk', 'build/programs.mk',
            '.github/workflows/build.yml', '.github/workflows/check.yml'])
import os
from pathlib import Path
import subprocess
import sys
import types
import tempfile
import shutil
import unittest
from unittest.mock import patch
import cc_cache



class Dispatch(unittest.TestCase):
    def call(self, cache, disabled):
        args = ['gcc', '-c', 'sqlite3.c', '-o', 'sqlite.o']
        original = args.copy()
        result = subprocess.CompletedProcess(args, 7)
        with patch.dict(os.environ, {'OS32_NO_CCACHE': disabled}), \
                patch.object(cc_cache.shutil, 'which', return_value=cache), \
                patch.object(cc_cache.subprocess, 'run', return_value=result) as run:
            self.assertIs(cc_cache.build(args, check=True, cwd='/work'), result)
        self.assertEqual(args, original)
        self.assertEqual(run.call_args.kwargs, dict(check=True, cwd='/work'))
        return run.call_args.args[0], args

    def test_enabled(self):
        got, args = self.call('/usr/bin/ccache', '')
        self.assertEqual(got, ['/usr/bin/ccache', 'compiler_check=content', 'base_dir=', 'sloppiness=', 'hash_dir=true', *args])

    def test_missing(self):
        got, args = self.call(None, '')
        self.assertEqual(got, args)

    def test_disabled(self):
        got, args = self.call('/usr/bin/ccache', '1')
        self.assertEqual(got, args)

    def test_failure(self):
        error = subprocess.CalledProcessError(2, ['gcc'])
        with patch.object(cc_cache.subprocess, 'run', side_effect=error):
            with self.assertRaises(subprocess.CalledProcessError):
                cc_cache.build(['gcc', '-c', 'sqlite3.c'], check=True)


class Runtime(unittest.TestCase):
    @unittest.skipUnless(shutil.which('ccache'), 'ccache is not installed')
    def test_bytes_and_mutation_hits(self):
        with tempfile.TemporaryDirectory(prefix='cc-dispatch-') as tmp:
            root = Path(tmp)
            a, b, exe = root/'a.c', root/'b.c', root/'host'
            a.write_text('extern int value(void); const char *path=__FILE__; int main(void) {return value();}\n')
            b.write_text('int value(void) {return 0;}\n')
            env = dict(os.environ, CCACHE_DIR=str(root/'cache'), OS32_NO_CCACHE='0')
            with patch.dict(os.environ, env):
                args = ['gcc', '-O2', str(a), str(b), '-o', str(exe)]
                subprocess.run(args, check=True)
                expected = exe.read_bytes()
                cc_cache.build(args, check=True)
                self.assertEqual(expected, exe.read_bytes())
                cc_cache.build(args, check=True)
                self.assertEqual(expected, exe.read_bytes())
                before = stats()
                b.write_text('int value(void) {return 1;}\n')
                cc_cache.build(args, check=True)
                self.assertEqual(subprocess.run([str(exe)]).returncode, 1)
                after = stats()
                self.assertGreater(after['direct_cache_hit'] + after['preprocessed_cache_hit'],
                                   before['direct_cache_hit'] + before['preprocessed_cache_hit'])
                # Absolute __FILE__, -g, and different compile CWDs retain
                # the exact bytes produced by the original compiler.
                for cwd in (root, root/'other'):
                    cwd.mkdir(exist_ok=True)
                    obj = root/'debug.o'
                    args = ['gcc', '-g', '-c', str(a), '-o', str(obj)]
                    subprocess.run(args, cwd=cwd, check=True)
                    expected = obj.read_bytes()
                    for _ in range(2):
                        cc_cache.build(args, cwd=cwd, check=True)
                        self.assertEqual(expected, obj.read_bytes())


def stats():
    return {key: int(value) for key, value in
            (line.split('\t') for line in subprocess.check_output(
                ['ccache', '--print-stats'], text=True).splitlines())}


def run(ctx):
    argv = [sys.executable, '-B', 'tools/tests/test_cc_cache.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)


if __name__ == '__main__':
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__]))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if '--mutate' in sys.argv:
        source = Path(cc_cache.__file__).read_text()
        anchor = "os.environ.get('OS32_NO_CCACHE') == '1'"
        assert source.count(anchor) == 1
        mutant = types.ModuleType('cc_cache_mutant')
        exec(compile(source.replace(anchor, 'False'), str(Path(cc_cache.__file__)), 'exec'), mutant.__dict__)
        cc_cache = mutant
        result = unittest.TestResult()
        Dispatch('test_disabled').run(result)
        assert len(result.failures) == 1 and not result.errors
        print('RED runtime: ignored OS32_NO_CCACHE')
        # Disabling per-TU dispatch preserves bytes but must lose the hit on
        # the unchanged TU when its sibling is mutated.
        cc_cache = types.ModuleType('cc_cache_no_split')
        anchor = "    sources = [i for i, a in enumerate(argv)"
        assert source.count(anchor) == 1
        changed = source.replace(anchor, "    return subprocess.run(argv, **kwargs)\n" + anchor)
        exec(compile(changed, str(Path(__file__)), 'exec'), cc_cache.__dict__)
        result = unittest.TestResult()
        Runtime('test_bytes_and_mutation_hits').run(result)
        assert len(result.failures) == 1 and not result.errors
        print('RED runtime: unchanged translation unit must hit')
