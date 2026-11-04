"""Host fixture cache selection, cross-worktree keys, and mutation checks."""
CHECK = dict(name='host32-cache-host', tier=1,
             covers=['tools/tests/host32.py', 'tools/tests/test_host32_cache.py'],
             needs=['make:all'], budget=5)

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import host32


class CacheTests(unittest.TestCase):
    def test_selection(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td) / 'tree'
            local = root / 'build/out/host32-fixtures'
            external = pathlib.Path(td) / 'external'
            env = {'HOME': td, 'XDG_CACHE_HOME': str(pathlib.Path(td) / 'xdg')}
            for extra, expected, outside in [
                ({'OS32_HOST32_CACHE': 'off'}, local, False),
                ({'OS32_HOST32_CACHE': str(external)}, external, True),
                ({'CI': '1'}, local, False),
                ({}, pathlib.Path(td) / 'xdg/os32/host32-fixtures', True),
                ({'OS32_HOST32_CACHE': str(pathlib.Path(td) / 'file/cache')}, local, False),
            ]:
                (pathlib.Path(td) / 'file').touch()
                with patch.dict(os.environ, dict(env, **extra), clear=True):
                    self.assertEqual(host32.fixture_cache(root), (expected, outside))
            with patch.dict(os.environ, {'HOME': td}, clear=True):
                self.assertEqual(host32.fixture_cache(root),
                                 (pathlib.Path(td) / '.cache/os32/host32-fixtures', True))

    def test_build_keys(self):
        with tempfile.TemporaryDirectory() as td:
            base = pathlib.Path(td)
            cache = base / 'cache'
            roots = [base / 'one', base / 'two']
            for root in roots:
                root.mkdir()
                (root / 'input.c').write_text('int value = 1;\n')
            def build(root, flags=()):
                with patch.object(host32, '__file__', str(root / 'tools/tests/host32.py')):
                    result = host32.build(['gcc', *flags, '-c', str(root / 'input.c'),
                                           '-o', str(root / 'out.o')])
                self.assertEqual(result.returncode, 0)
            def count():
                return len([p for p in cache.iterdir() if p.name != '.lock'])
            with patch.dict(os.environ, {'OS32_HOST32_CACHE': str(cache)}):
                build(roots[0]); build(roots[1])
                self.assertEqual(count(), 1)
                build(roots[0], ['-g']); build(roots[1], ['-g'])
                self.assertEqual(count(), 3)
                (roots[1] / 'input.c').write_text('int value = 2;\n')
                build(roots[1])
                self.assertEqual(count(), 4)
            # A per-run temporary include directory that ends the argument still
            # shares one entry (each test creates its own mkdtemp under TMPDIR).
            runs = []
            for _ in range(2):
                inc = pathlib.Path(tempfile.mkdtemp(prefix='os32-h32-'))
                self.addCleanup(shutil.rmtree, inc, True)
                (inc / 'value.h').write_text('#define VALUE 3\n')
                runs.append(inc)
            (roots[0] / 'inc.c').write_text('#include "value.h"\nint v = VALUE;\n')
            with patch.dict(os.environ, {'OS32_HOST32_CACHE': str(cache)}):
                before = count()
                for inc in runs:
                    with patch.object(host32, '__file__', str(roots[0] / 'tools/tests/host32.py')):
                        r = host32.build(['gcc', '-I' + str(inc), '-c', str(roots[0] / 'inc.c'),
                                          '-o', str(roots[0] / 'inc.o')])
                    self.assertEqual(r.returncode, 0)
                self.assertEqual(count(), before + 1)

    def test_command_spelling(self):
        root = pathlib.Path('/w/one')
        other = pathlib.Path('/w/two')
        def key(r, args, cwd):
            return host32.cache_command([a.replace('<r>', str(r)) for a in args], r, cwd)
        inputs = ['gcc', '-I<r>/inc', '<r>/a.c', '<r>/lib.a']
        self.assertEqual(key(root, inputs, root), key(other, inputs, other))
        rpath = ['gcc', '<r>/a.c', '-Wl,-rpath,<r>/lib']
        self.assertNotEqual(key(root, rpath, root), key(other, rpath, other))
        for split in (['-Xlinker', '-rpath', '-Xlinker', '<r>/lib'], ['-L', '<r>/lib']):
            args = ['gcc', '<r>/a.c', *split]
            self.assertNotEqual(key(root, args, root), key(other, args, other))
        debug = ['gcc', '-g', '-c', 'a.c']
        self.assertNotEqual(key(root, debug, root), key(other, debug, other))

    def test_limits(self):
        with tempfile.TemporaryDirectory() as td:
            cache = pathlib.Path(td)
            for i in range(3):
                (cache / str(i)).write_text('x')
            with patch.object(host32, 'CACHE_ENTRIES', 1):
                host32.prune_fixtures(cache, True)
                self.assertEqual(len(list(cache.iterdir())), 3)
                host32.prune_fixtures(cache)
                self.assertEqual(len(list(cache.iterdir())), 1)


def mutations():
    source = pathlib.Path(host32.__file__).read_text()
    changes = [
        ("lambda arg: stable(\n        arg.replace(str(root), '<root>'))", "lambda arg: stable(arg)"),
        ("return ' '.join(preprocess) + ' cwd=' + str(cwd)", "pass"),
        ("arg if keep or arg.startswith(SPELLED_PREFIXES) else share(arg)", "share(arg)"),
        ("keep = arg in SPELLED_NEXT", "keep = False"),
        ("except OSError:\n            cache = local", "except OSError:\n            raise"),
        ("r'/[^/\\s]+(?=/|\\s|$)'", "r'/[^/\\s]+/'"),
    ]
    ok = True
    for number, (old, new) in enumerate(changes, 1):
        if source.count(old) != 1:
            print(f'MUTATION {number} NOT_APPLIED', flush=True)
            ok = False
            continue
        with tempfile.TemporaryDirectory() as td:
            path = pathlib.Path(td)
            (path / 'host32.py').write_text(source.replace(old, new))
            shutil.copy2(__file__, path / 'test_host32_cache.py')
            result = subprocess.run([sys.executable, '-B', str(path / 'test_host32_cache.py')],
                                    capture_output=True, text=True)
            red = result.returncode == 1 and ('FAIL:' in result.stderr or 'ERROR:' in result.stderr)
            print(f'MUTATION {number} {"RED" if red else "UNEXPECTED"}', flush=True)
            ok &= red
    return ok


if __name__ == '__main__':
    mutate = '--mutate' in sys.argv
    if mutate:
        sys.argv.remove('--mutate')
    result = unittest.main(exit=False).result
    sys.exit(0 if result.wasSuccessful() and (not mutate or mutations()) else 1)


def run(ctx) -> int:
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_host32_cache.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
