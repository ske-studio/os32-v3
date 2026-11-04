#!/usr/bin/env python3
"""Translation-unit result cache contracts and runtime mutation checks."""
CHECK = dict(name='check-result-cache-host', tier=1,
    covers=['tools/prototypes/geforce_imports.def', 'tools/prototypes/v86_imports.def', 'tools/clang_ast/**', 'tools/gen_hwdev_pair.py', 'tools/tests/test_clang_ast.py', '**/*.[ch]', '**/*.inc', 'sdk/example/hello/**', 'tools/check_c_dialect.py', 'tools/gen_sqlite_imports.py', 'tools/gen_kdebug_imports.py', 'tools/prototypes/sqlite_imports.def', 'tools/prototypes/fep_imports.def', 'tools/prototypes/cd_imports.def', 'tools/prototypes/pcm_imports.def', 'tools/prototypes/lan_imports.def', 'tools/prototypes/cirrus_imports.def', 'build/sqlite-proto.mk', 'tools/tests/mutpar.py', 'sdk/kapi.json', 'sdk/rust/os32api/src/kapi_generated.rs', 'sdk/gen_kapi.py', 'build/kernel.mk', 'build/libs.mk', 'build/os32.ld', 'build/programs.mk', 'sdk/kapi_rust_gen.py', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/generations.ld', 'sdk/link/shlib.ld', 'sdk/os32_generations.py', 'sdk/rust/os32api/src/generations.rs', 'tools/pack_name_tables.txt', 'tools/pack_notes.txt', 'tools/gen_memmap.py', 'boot/loader_fat_new.asm', 'arch/x86/kernel/kentry.asm', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/mkshlib.py', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'kernel/module/kdebug_imports.def', 'tools/mkmod.py', 'tools/module_schema.py', 'tools/tests/kernel_includes.py', 'tools/check_arch_asm.py', 'tools/check_le_access.py', 'tools/check_p2v.py', 'tools/check_p2v_allow.txt', 'tools/source_roots.py', 'tools/checks/c-dialect.py', 'tools/checks/p2v.py', 'tools/checks/arch-asm.py', 'tools/tests/test_c_dialect.py', 'tools/tests/test_check_result_cache.py', 'tools/tests/test_p2v.py'],
    needs=['make:all'], budget=15.059894)

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--mutate', action='store_true')
parser.add_argument('--tools', type=pathlib.Path, default=ROOT / 'tools')
ARGS = parser.parse_args() if __name__ == '__main__' else parser.parse_args([])
sys.path.insert(0, str(ARGS.tools))
import clang_ast as ast
from clang_ast import result_cache as cache
import check_c_dialect as cd


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='check_result_test_')
        self.addCleanup(self.tmp.cleanup)
        self.base = pathlib.Path(self.tmp.name)
        self.root = self.base / 'source'
        self.root.mkdir()
        (self.root / 'unit.c').write_text('#include "header.h"\nint value;\n')
        (self.root / 'header.h').write_text('/* header */\n#define UNUSED 1\n')
        self.directory = self.base / 'cache'
        self.env = mock.patch.dict(os.environ, OS32_CHECK_CACHE=str(self.directory))
        self.env.start()
        self.addCleanup(self.env.stop)
        cache._WRITE_FAILED = False
        cache._checker_source_digest.cache_clear()
        self.argv = ['-std=gnu11', '-ffreestanding', '-I' + str(self.root)]
        self.calls = 0

    def compute(self):
        self.calls += 1
        ast.parse('unit.c', self.argv, self.root)
        return {'findings': [], 'functions': [['unit.c', 'f']],
                'scanned': ['unit.c'], 'includes': ['header.h']}

    def get(self, **kwargs):
        return cache.get_or_compute(kwargs.get('tool', 'c-dialect'), 'unit.c',
                                    self.argv, self.root, kwargs.get('compute', self.compute))

    def changed(self, edit):
        before = self.get()
        calls = self.calls
        edit()
        self.assertEqual(self.get(), before)
        self.assertEqual(self.calls, calls + 1)

    def test_hit_copy_and_parse_count(self):
        with mock.patch.object(ast, 'parse', wraps=ast.parse) as parse:
            value = self.get()
            self.assertEqual(self.get(), value)
            self.assertEqual(parse.call_count, 1)
            copy = self.base / 'copy'
            shutil.copytree(self.root, copy)
            self.root = copy
            self.argv[-1] = '-I' + str(copy)
            self.assertEqual(self.get(), value)
            self.assertEqual(parse.call_count, 1)

    def test_expanded_file_string(self):
        (self.root / 'unit.c').write_text('const char *where = __FILE__;\n')
        self.get()
        copy = self.base / 'longer-copy'
        shutil.copytree(self.root, copy)
        self.root = copy
        self.argv[-1] = '-I' + str(copy)
        self.get()
        self.assertEqual(self.calls, 2)

    def test_source_comment(self):
        self.changed(lambda: (self.root / 'unit.c').write_text(
            '#include "header.h"\nint value; /* changed */\n'))

    def test_dependency_raw_bytes(self):
        self.changed(lambda: (self.root / 'header.h').write_text(
            '/* headeR */\n#define UNUSED 1\n'))
        self.changed(lambda: (self.root / 'header.h').write_text(
            '/* headeR */\n#define UNUSED 2\n'))

    def test_new_include_and_has_include(self):
        (self.root / 'new.h').write_text('/* new */\n')
        self.changed(lambda: (self.root / 'unit.c').write_text(
            '#include "header.h"\n#include "new.h"\nint value;\n'))
        (self.root / 'unit.c').write_text(
            '#if __has_include("optional.h")\nint option;\n#endif\n')
        self.changed(lambda: (self.root / 'optional.h').write_text('/* optional */\n'))

    def test_original_and_effective_arguments(self):
        self.changed(lambda: self.argv.append('-DUNUSED_ARG=1'))
        original = ast.flags
        self.changed_args = original(self.argv, self.root) + ['-DANOTHER_UNUSED=1']
        with mock.patch.object(ast, 'flags', return_value=self.changed_args):
            self.get()
        self.assertEqual(self.calls, 3)
        a = cache._arguments(['-D', 'VALUE="' + str(self.root) + '"'], self.root)
        self.assertEqual(a, ['-D', 'VALUE="' + str(self.root) + '"'])
        self.assertNotEqual(cache._arguments(['-DVALUE="' + str(self.root) + '"'], self.root),
                            cache._arguments(['-DVALUE="' + str(self.base / 'copy') + '"'], self.root))

    def test_identities_and_tool_namespace(self):
        self.get()
        with mock.patch.object(cache, '_checker_source_digest', return_value='new checker'):
            self.get()
        self.assertEqual(self.calls, 2)
        identity = cache._compiler_identity()
        for index in (0, 1, 3):
            changed = list(identity)
            changed[index] += ' changed'
            with mock.patch.object(cache, '_compiler_identity', return_value=changed):
                self.get()
        self.assertEqual(self.calls, 5)
        with mock.patch.object(cache, '_checker_source_digest', return_value='same'):
            self.get()
            self.get(tool='p2v')
            self.get(tool='p2v')
        self.assertEqual(self.calls, 7)

    def test_checker_and_shared_contents_digest(self):
        original = pathlib.Path.read_bytes
        for target in ('check_c_dialect.py', 'source_roots.py', 'clang_ast/dialect.py',
                       'clang_ast/type_occurrences.cpp'):
            cache._checker_source_digest.cache_clear()
            before = cache._checker_source_digest('c-dialect')
            def read(p):
                data = original(p)
                return data + b'\n# change\n' if p == ARGS.tools / target else data
            with mock.patch.object(pathlib.Path, 'read_bytes', read):
                cache._checker_source_digest.cache_clear()
                self.assertNotEqual(cache._checker_source_digest('c-dialect'), before)
        cache._checker_source_digest.cache_clear()

    def test_corruption(self):
        value = self.get()
        path, = self.directory.rglob('*.json')
        good = path.read_bytes()
        broken = [b'', b'{', b'[]']
        entry = json.loads(good)
        for field, wrong in [('format', '1'), ('tool', 'p2v'), ('key', 'wrong'),
                             ('result', 'wrong'), ('digest', 'wrong')]:
            broken.append(json.dumps(dict(entry, **{field: wrong})).encode())
        for field in entry:
            other = dict(entry)
            del other[field]
            broken.append(json.dumps(other).encode())
        for data in broken:
            path.write_bytes(data)
            calls = self.calls
            self.assertEqual(self.get(), value)
            self.assertEqual(self.calls, calls + 1)

    def test_off_and_ci_do_no_cache_work(self):
        for env in ({'OS32_CHECK_CACHE': 'off'}, {'CI': '1'}):
            with mock.patch.dict(os.environ, env, clear=True), \
                    mock.patch.object(cache, '_internal_cache_key', side_effect=AssertionError('key used')), \
                    mock.patch.object(cache, '_read', side_effect=AssertionError('I/O used')):
                self.assertEqual(cache.get_or_compute('c-dialect', 'unit.c', [], self.root,
                                                     lambda: ['checked']), ['checked'])
        with mock.patch.dict(os.environ, CI='1'):
            self.get()
            self.get()
            self.assertEqual(self.calls, 1)

    def test_unwritable_and_key_failure(self):
        # An existing regular file cannot be a directory, even when run as root.
        self.directory.write_text('not a directory')
        self.assertEqual(self.get(), self.get())
        self.assertEqual(self.calls, 2)
        self.assertTrue(cache._WRITE_FAILED)
        with mock.patch.object(cache, '_internal_cache_key', side_effect=OSError('unavailable')):
            self.get()
        self.assertEqual(self.calls, 3)

    def test_exceptions_are_not_saved(self):
        for _ in range(2):
            with self.assertRaises(ast.ParseError):
                self.get(compute=lambda: ast.parse('unit.c', ['-std=invalid'], self.root))
        self.assertEqual(list(self.directory.rglob('*.json')), [])
        with self.assertRaises(RuntimeError):
            self.get(compute=lambda: (_ for _ in ()).throw(RuntimeError('interrupted')))
        self.assertEqual(list(self.directory.rglob('*.json')), [])

    def test_c_dialect_findings_are_cached(self):
        (self.root / 'unit.c').write_text('int f(int *restrict p);\n')
        unit = dict(src='unit.c', argv=self.argv)
        with mock.patch.object(ast, 'parse', wraps=ast.parse) as parse:
            result = cd.check_internal_units([unit], self.root)
            self.assertTrue(any('restrict' in p for p in result[0]))
            self.assertEqual(cd.check_internal_units([unit], self.root), result)
            self.assertEqual(parse.call_count, 1)
            (self.root / 'unit.c').write_text('_Atomic int value;\n')
            updated = cd.check_internal_units([unit], self.root)
            self.assertTrue(any('_Atomic' in p for p in updated[0]))
            self.assertEqual(parse.call_count, 2)
            with mock.patch.dict(os.environ, OS32_CHECK_CACHE='off'):
                self.assertEqual(cd.check_internal_units([unit], self.root), updated)

    def test_vendor_spelling_is_not_shared(self):
        # The vendor exemption reads the spelling of src; the key reads its bytes.
        vendor = self.root / 'lib/zlib'
        vendor.mkdir(parents=True)
        (vendor / 'a.c').write_text('#include "inner.h"\n')
        (self.root / 'inner.h').write_text('int f(int *restrict p);\n')
        spellings = [dict(src='lib/zlib/a.c', argv=self.argv),
                     dict(src=str(vendor / 'a.c'), argv=self.argv)]
        with mock.patch.dict(os.environ, OS32_CHECK_CACHE='off'):
            off = [cd.check_internal_units([u], self.root)[0] for u in spellings]
        self.assertNotEqual(off[0], off[1])
        self.assertEqual([cd.check_internal_units([u], self.root)[0] for u in spellings], off)


MUTATIONS = [
    ('dependency bytes', 'hashlib.sha256(p.read_bytes()).hexdigest())\n                   for p in sorted(paths',
     "'ignored')\n                   for p in sorted(paths"),
    ('arguments', '_arguments(argv, root), _arguments(args, root),', '[], [],'),
    ('checker hash', '_checker_source_digest(tool), variant]', "'ignored', variant]"),
    ('variant', '_checker_source_digest(tool), variant]', '_checker_source_digest(tool), None]'),
    ('compiler identity', '_compiler_identity(), _checker_source_digest(tool)',
     '[], _checker_source_digest(tool)'),
    ('expanded output', 'inputs.append(hashlib.sha256(expanded).hexdigest())',
     "inputs.append('ignored')"),
    ('digest verification', "entry['digest'] != _digest(entry['result'])", 'False'),
    ('cache hit', 'if result is not None:\n        return result',
     'if result is not None:\n        return compute()'),
    ('off', "override == 'off' or (override is None and os.environ.get('CI'))", 'False'),
    ('save findings', '    result = compute()\n    _write(path, tool, key, result)',
     "    result = compute()\n    result = [] if tool == 'c-dialect' else result\n    _write(path, tool, key, result)"),
]


def main():
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(CacheTests))
    if not result.wasSuccessful():
        return 1
    if ARGS.mutate:
        text = (ARGS.tools / 'clang_ast/result_cache.py').read_text()
        for name, old, new in MUTATIONS:
            assert text.count(old) == 1, 'mutation anchor: ' + name
            with tempfile.TemporaryDirectory(prefix='result_cache_mut_') as td:
                tools = pathlib.Path(td) / 'tools'
                shutil.copytree(ARGS.tools / 'clang_ast', tools / 'clang_ast',
                                ignore=shutil.ignore_patterns('__pycache__'))
                for filename in ('check_c_dialect.py', 'source_roots.py'):
                    shutil.copy2(ARGS.tools / filename, tools / filename)
                (tools / 'clang_ast/result_cache.py').write_text(text.replace(old, new))
                r = subprocess.run([sys.executable, '-B', str(pathlib.Path(__file__).resolve()),
                                    '--tools', str(tools)],
                                   cwd=td, capture_output=True, text=True)
                runtime_red = (r.returncode == 1 and 'FAIL:' in r.stderr
                               and 'SyntaxError' not in r.stderr and 'ImportError' not in r.stderr)
                print('MUTATION %s: %s' % (name, 'RED' if runtime_red else 'FAILED'), flush=True)
                if not runtime_red:
                    print(r.stdout + r.stderr)
                    return 1
        print('MUTATIONS %d/%d RED' % (len(MUTATIONS), len(MUTATIONS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())


def run(ctx):
    argv = [sys.executable, '-B', 'tools/tests/test_check_result_cache.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)
