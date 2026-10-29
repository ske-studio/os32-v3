"""L0 tools: real git moves, quoted-include identity and binary comparison.

python3 -B tools/tests/test_src_layout.py [--mutate]
Mutants run the same normal cases in small independent trees.
"""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(os.environ.get('OS32_LAYOUT_TEST_TOOLS', ROOT / 'tools'))


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def command(*args, cwd=None, ok=True, env=None):
    result = subprocess.run([str(a) for a in args], cwd=cwd, env=env,
                            capture_output=True, text=True)
    if ok and result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result


class LayoutTools(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='src-layout-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        command('git', 'init', '-q', cwd=self.root)

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def fixture(self):
        self.write('kernel/main.c', '#include "../include/api.h"\n#include "local.h"\n')
        self.write('kernel/local.h', '#include "../include/api.h"\n')
        self.write('include/api.h', '/* API */\n')
        self.write('exec/client.c', '#include "../kernel/local.h"\n')
        self.write('notes.txt', 'kernel/main.c kernel/main.cx kernel/local.h\n')
        self.write('tools/test.py', "p = ROOT / 'kernel' / 'main.c'\n")
        self.write('docs/archive/old.txt', 'kernel/main.c\n')
        self.write('third_party/vendor.c', '/* kernel/main.c */\n')
        self.write('kernel/kdebug_imports.def', '#include "kernel/local.h"\n')
        self.write('kdebug/generated/kdebug_imports.h', '#include "../../kernel/local.h"\n')
        self.write('Makefile', 'kdebug/generated/kdebug_imports.h: kernel/kdebug_imports.def\n'
                   '\tsed \'s|include "|include "../../|\' $< > $@\n')
        self.write('build/config.mk', 'INC_KERNEL = -Iinclude -Ikernel -Idrivers\n')
        command('git', 'add', '.', cwd=self.root)
        command('git', '-c', 'user.name=fixture', '-c', 'user.email=fixture@example.invalid',
                '-c', 'commit.gpgsign=false', 'commit', '-qm', 'fixture', cwd=self.root)
        moves = [{'from': 'kernel/main.c', 'to': 'mm/main.c'},
                 {'from': 'kernel/local.h', 'to': 'mm/local.h'}]
        mapping = self.write('moves.json', json.dumps(moves))
        return moves, mapping

    def include_rows(self):
        module = load('include_resolve')
        return module.resolve(self.root, module.tracked_files(self.root), {}, ['include', 'kernel', 'mm'])

    def test_move_dry_run_apply_and_resolution(self):
        moves, mapping = self.fixture()
        before = self.include_rows()
        result = command(sys.executable, TOOLS / 'move_files.py', '--root', self.root, '--dry-run', mapping)
        plan = json.loads(result.stdout)
        self.assertEqual(plan['moves'], {m['from']: m['to'] for m in moves})
        self.assertTrue(plan['replacements'])
        self.assertEqual(plan['split_paths'][0]['path'], 'kernel/main.c')
        self.assertTrue((self.root / 'kernel/main.c').exists())
        self.assertIn('kernel/main.c', (self.root / 'notes.txt').read_text())
        command(sys.executable, TOOLS / 'move_files.py', '--root', self.root, mapping)
        self.assertFalse((self.root / 'kernel/main.c').exists())
        self.assertEqual((self.root / 'notes.txt').read_text(), 'mm/main.c kernel/main.cx mm/local.h\n')
        self.assertIn('R  kernel/main.c -> mm/main.c', command('git', 'status', '--short', cwd=self.root).stdout)
        self.assertEqual((self.root / 'docs/archive/old.txt').read_text(), 'kernel/main.c\n')
        self.assertEqual((self.root / 'third_party/vendor.c').read_text(), '/* kernel/main.c */\n')
        self.assertIn('-Ikernel -Imm', (self.root / 'build/config.mk').read_text())
        after = self.include_rows()
        mapping = {m['from']: m['to'] for m in moves}
        identity = lambda rows: {(mapping.get(r['file'], r['file']), r['line'],
                                  mapping.get(r['target'], r['target'])) for r in rows}
        self.assertEqual(identity(before), identity(after))
        self.assertFalse(any(r['target'] is None for r in after))

    def test_include_local_precedence_and_inactive_lines(self):
        self.write('include/local.h', '/* external */\n')
        self.write('src/local.h', '/* local */\n')
        self.write('src/main.c', '/* #include "missing.h" */\n#if 0\n\n#include "local.h"\n#endif\n'
                   '#include "unknown.h"\n')
        rows = load('include_resolve').resolve(self.root, ['src/main.c'], {}, ['include'])
        self.assertEqual([(r['line'], r['target']) for r in rows], [(4, 'src/local.h'), (6, None)])
        # One shared header can resolve differently in two compilation profiles.
        self.write('other/local.h', '/* other */\n')
        self.write('common/shared.h', '#include "local.h"\n')
        self.write('src/one.c', '#include "../common/shared.h"\n')
        profiles = {('src/one.c', 'core.o'): {(self.root / 'include',)},
                    ('src/one.c', 'r1.o'): {(self.root / 'other',)}}
        rows = load('include_resolve').resolve(self.root, [], profiles)
        self.assertEqual({r['target'] for r in rows if r['file'] == 'common/shared.h'},
                         {'include/local.h', 'other/local.h'})
        self.assertEqual({(r['unit'], r['target']) for r in rows if r['file'] == 'common/shared.h'},
                         {('core.o', 'include/local.h'), ('r1.o', 'other/local.h')})

    def test_move_output_prefixed_objects_and_report_unresolved_paths(self):
        self.write('kernel/module.c', '/* module */\n')
        # The OBJECTS line reproduces tools/tests/test_ver_version.py:14.
        source = "OBJECTS = ('build/out/obj/kernel/ver_format.o', 'build/out/obj/kernel/module.o')\n"
        prefixes = ('build/out/obj/', 'build/out/r1/obj/', 'build/out/kernel-r1/obj/', 'custom/output/obj/',
                    'obj/', '../build/out/obj/', '/tmp/layout-output/obj/',
                    '$(BUILD_OUT)/obj/', '${BUILD_OUT}/obj/', '$BUILD_OUT/obj/')
        source += ''.join(f"'{prefix}kernel/module{suffix}'\n"
                          for prefix in prefixes for suffix in ('.o', '.d'))
        source += "'build/out/obj/kernel/module.obj' 'build/out/obj/kernel/module.o.extra'\n"
        source += "'build/out/obj/mykernel/module.o'\n"
        expected = source.replace("obj/kernel/module.o'", "obj/kernel/module/module.o'")
        expected = expected.replace("obj/kernel/module.d'", "obj/kernel/module/module.d'")
        unresolved = ("p = ROOT / 'build/out/obj' / 'kernel' / 'module.o'",
                      "p = ROOT / 'kernel' / 'module.d'",
                      "p = 'unrecognized/kernel/module.o'",
                      "p = '$(call output)/obj/kernel/module.d'")
        source += '\n'.join(unresolved) + '\n'
        expected += '\n'.join(unresolved) + '\n'
        self.write('tools/tests/test_ver_version.py', source)
        command('git', 'add', '.', cwd=self.root)
        mapping = self.write('moves.json', json.dumps([
            {'from': 'kernel/module.c', 'to': 'kernel/module/module.c'}]))
        dry = command(sys.executable, TOOLS / 'move_files.py', '--root', self.root, '--dry-run', mapping)
        report = json.loads(dry.stdout)
        self.assertTrue(report['replacements'], 'output-prefixed object references were not replaced')
        edit = next(e for e in report['replacements'] if e['file'] == 'tools/tests/test_ver_version.py')
        self.assertIn("+OBJECTS = ('build/out/obj/kernel/ver_format.o', 'build/out/obj/kernel/module/module.o')",
                      edit['diff'])
        self.assertEqual((self.root / 'tools/tests/test_ver_version.py').read_text(), source)
        applied = command(sys.executable, TOOLS / 'move_files.py', '--root', self.root, mapping)
        self.assertEqual(json.loads(applied.stdout), report)
        self.assertEqual((self.root / 'tools/tests/test_ver_version.py').read_text(), expected)
        self.assertFalse((self.root / 'kernel/module.c').exists())
        self.assertTrue((self.root / 'kernel/module/module.c').is_file())
        warnings = report['split_paths'] + report['unresolved_paths']
        self.assertEqual({r['text'] for r in warnings}, set(unresolved))
        for row in warnings:
            self.assertEqual(row['file'], 'tools/tests/test_ver_version.py')
            self.assertEqual(source.splitlines()[row['line'] - 1], row['text'])
        self.assertEqual({r['path'] for r in warnings}, {'kernel/module.o', 'kernel/module.d'})

    def test_move_assembly_outputs_with_old_path_in_destination(self):
        moves = {'kernel/entry.asm': 'arch/x86/kernel/entry.asm',
                 'kernel/stub.S': 'arch/x86/kernel/stub.S'}
        source = 'build/out/obj/kernel/entry.o $(BUILD_OUT)/obj/kernel/stub.d\n'
        expected = 'build/out/obj/arch/x86/kernel/entry.o $(BUILD_OUT)/obj/arch/x86/kernel/stub.d\n'
        for old in moves:
            self.write(old, '/* assembly */\n')
        self.write('objects.mk', source)
        command('git', 'add', '.', cwd=self.root)
        module = load('move_files')
        edits, split_paths, unresolved_paths, _ = module.plan(self.root, moves)
        self.assertEqual(next(e['after'] for e in edits if e['file'] == 'objects.mk'), expected)
        self.assertEqual(split_paths, [])
        self.assertEqual(unresolved_paths, [])

    def test_rebase_include_of_unmoved_header(self):
        module = load('move_files')
        actual = module.rewrite('#include "../include/api.h"\n', 'kernel/main.c',
                                {'kernel/main.c': 'arch/x86/kernel/main.c'})
        self.assertEqual(actual, '#include "../../../include/api.h"\n')
        self.write('kernel/unchanged.h', '/* unchanged */\n')
        actual = module.rewrite('#include "unchanged.h"\n', 'kernel/main.c',
                                {'kernel/main.c': 'mm/main.c'}, self.root)
        self.assertEqual(actual, '#include "../kernel/unchanged.h"\n')
        self.assertEqual(module.rewrite('kernel/main.o kernel/main.d kernel/main.obj\n', 'build/example.mk',
                                       {'kernel/main.c': 'mm/main.c'}),
                         'mm/main.o mm/main.d kernel/main.obj\n')

    def test_compare_bytes_inventory_and_function_differences(self):
        module = load('build_compare')
        left, right = self.root / 'a', self.root / 'b'
        for tree in (left, right):
            for path in module.artifacts(tree):
                target = tree / 'build/out' / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b'fixed')
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(module.compare(left, right), [])
            (right / 'build/out/kernel.bin').write_bytes(b'changed')
            self.assertEqual(module.compare(left, right), ['kernel.bin'])
            (left / 'build/out/extra.mod').write_bytes(b'new')
            self.assertEqual(module.compare(left, right), ['extra.mod', 'kernel.bin'])
        for tree, number, obj in ((left, 1, 'kernel/example.o'), (right, 2, 'build/out/obj/kernel/example.o')):
            source = tree / 'example.c'
            source.write_text(f'int changed(void) {{ return {number}; }}\nint stable(void) {{ return 3; }}\n')
            output = tree / obj
            output.parent.mkdir(parents=True, exist_ok=True)
            command('gcc', '-c', '-O0', source, '-o', output)
        diff = module.function_diff(left, right, shutil.which('objdump'))
        self.assertIn('kernel/example.o: changed', diff)
        self.assertNotIn('kernel/example.o: stable', diff)

    def test_snapshot_uses_staged_moves_edits_and_new_files(self):
        self.write('tools/gen_build_id.py', '# OS32_BUILD_COMMIT hook\n')
        self.write('old.c', 'old\n')
        self.write('deleted.c', 'delete\n')
        command('git', 'add', '.', cwd=self.root)
        command('git', '-c', 'user.name=fixture', '-c', 'user.email=fixture@example.invalid',
                '-c', 'commit.gpgsign=false', 'commit', '-qm', 'fixture', cwd=self.root)
        command('git', 'mv', 'old.c', 'new.c', cwd=self.root)
        self.write('new.c', 'edited\n')
        (self.root / 'deleted.c').unlink()
        self.write('fresh.c', 'fresh\n')
        self.write('.env', 'fixture only\n')
        output = self.root / 'snapshot'
        load('build_compare').snapshot(self.root, 'HEAD', output)
        self.assertFalse((output / 'old.c').exists())
        self.assertFalse((output / 'deleted.c').exists())
        self.assertFalse((output / '.env').exists())
        self.assertEqual((output / 'new.c').read_text(), 'edited\n')
        self.assertEqual((output / 'fresh.c').read_text(), 'fresh\n')

    def test_standard_epoch_pins_compiler_date(self):
        source = self.write('stamp.c', 'const char stamp[] = __DATE__ " " __TIME__;\n')
        objects = []
        for name in ('first.o', 'second.o'):
            output = self.root / name
            command('gcc', '-c', source, '-o', output,
                    env=dict(os.environ, SOURCE_DATE_EPOCH='946684800'))
            objects.append(output.read_bytes())
        self.assertEqual(objects[0], objects[1])
        self.assertIn(b'Jan  1 2000 00:00:00', objects[0])

    def test_build_id_override_and_default(self):
        result = command(sys.executable, TOOLS / 'gen_build_id.py', '--repo', self.root, '--print',
                         env=dict(os.environ, OS32_BUILD_COMMIT='layout-fixed'))
        self.assertEqual(result.stdout.strip(), 'layout-fixed')
        env = dict(os.environ)
        env.pop('OS32_BUILD_COMMIT', None)
        result = command(sys.executable, TOOLS / 'gen_build_id.py', '--repo', self.root, '--print', env=env)
        self.assertEqual(result.stdout.strip(), 'unknown')


def mutations():
    cases = [
        ('move_files.py', "BOUNDARY = r'A-Za-z0-9_./+\\-'", "BOUNDARY = r'_'"),
        ('move_files.py', 'destination = moves.get(filename, filename)', 'destination = filename'),
        ('move_files.py', 'for match in pattern.finditer(text):', 'for match in []:'),
        ('move_files.py', 'for match in remaining_pattern.finditer(content):', 'for match in []:'),
        ('include_resolve.py', '(file.parent, *directories)', '(*directories, file.parent)'),
        ('build_compare.py', 'a.read_bytes() == b.read_bytes()', 'True'),
        ('gen_build_id.py', '        return fixed', "        return 'unknown'"),
    ]
    for filename, old, new in cases:
        with tempfile.TemporaryDirectory(prefix='layout-mutant-') as directory:
            tools = Path(directory)
            for name in ('move_files.py', 'include_resolve.py', 'build_compare.py', 'gen_build_id.py'):
                shutil.copy2(TOOLS / name, tools / name)
            text = (tools / filename).read_text()
            assert text.count(old) == 1, (filename, old)
            (tools / filename).write_text(text.replace(old, new, 1))
            result = command(sys.executable, '-B', Path(__file__), ok=False,
                             env=dict(os.environ, OS32_LAYOUT_TEST_TOOLS=str(tools)))
            assert result.returncode, 'mutant survived: ' + filename
            assert 'FAILED (failures=' in result.stderr, result.stdout + result.stderr
            print('RED ' + filename + ': ' + old, flush=True)


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(LayoutTools)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        raise SystemExit(1)
    if '--mutate' in sys.argv:
        mutations()
