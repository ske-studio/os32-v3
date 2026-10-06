"""ci-select regressions: isolated repositories; no emulator or live tree mutation."""
import copy
import pathlib
import subprocess
import sys
import unittest

import test_check_select as legacy

import os
BEFORE = '--before' in sys.argv
CS = legacy.load()
if os.environ.get('CHECK_SELECT_BEFORE'):
    CS = legacy.load(pathlib.Path(os.environ['CHECK_SELECT_BEFORE']).read_text())
if '--before' in sys.argv:
    sys.argv.remove('--before')
    CS = legacy.load(subprocess.check_output(['git', 'show', 'HEAD:tools/check_select.py'], text=True))


class CheckInfra(unittest.TestCase):
    def test_worktree_base(self):
        with legacy.fixture(CS) as fx:
            legacy.git(fx.d, 'checkout', '-qb', 'wt/new')
            fx.write('new.txt', 'new')
            old = CS.ROOT
            try:
                CS.ROOT = fx.d
                head = CS.git('rev-parse', 'HEAD')[0]
                self.assertEqual(CS.default_base()[0], head)
                self.assertEqual(CS.changed_files(head), ([], ['new.txt']))
            finally:
                CS.ROOT = old

    def test_worktree_with_parent(self):
        with legacy.fixture(CS) as fx:
            fx.write('old.txt', 'landed')
            legacy.git(fx.d, 'add', '.')
            legacy.git(fx.d, 'commit', '-qm', 'landed')
            legacy.git(fx.d, 'checkout', '-qb', 'wt/new')
            old = CS.ROOT
            try:
                CS.ROOT = fx.d
                self.assertEqual(CS.default_base()[0], CS.git('rev-parse', 'HEAD')[0])
            finally:
                CS.ROOT = old

    def test_host32_and_phony(self):
        with legacy.fixture(CS) as fx:
            fx.add_c(rule='check-c:\n\t$(call host32_check,test_c.py)\n.PHONY: check-c\n')
            self.assertEqual(fx.plan()[0], 'sel')
            self.assertEqual(fx.plan()[2], ['check-c'])

    def test_phony_hijack(self):
        with legacy.fixture(CS) as fx:
            fx.add_c()
            fx.edit('build/sdk.mk', 'check-a:\n', 'check-a:\n.PHONY: check-c\n')
            self.assertEqual(fx.plan()[0], 'full')

    def test_phony_trailing_recipe(self):
        with legacy.fixture(CS) as fx:
            fx.add_c(rule=legacy.C_RULE+'.PHONY: check-c\n'+legacy.A2)
            self.assertEqual(fx.plan()[0], 'full')

    def test_shard_addition(self):
        files = {'build/sdk.mk': 'CHECK_PAR_ORDER :=\n',
                 'build/checks.d/check-a.mk': 'CHECK_PAR_ORDER += 001:check-a\ncheck-a:\n'+legacy.A_LINE}
        with legacy.fixture(CS, files=files) as fx:
            fx.write('build/checks.d/check-c.mk', 'CHECK_PAR_ORDER += 002:check-c\n'+legacy.C_RULE+'.PHONY: check-c\n')
            mapping = copy.deepcopy(legacy.FX_MAP)
            mapping['full'].append('build/checks.d/*.mk')
            self.assertEqual(fx.plan(map_=mapping)[0], 'sel')
            self.assertEqual(fx.plan(map_=mapping)[2], ['check-c'])

    def test_auto_headers(self):
        with legacy.fixture(CS, files={'tools/tests/test_a.py': 'SRC="tools/tests/a.c"\n',
                                     'tools/tests/a.c': '#include <indirect.h>\n',
                                     'indirect.h': '#define N 1\n'}) as fx:
            mapping = copy.deepcopy(legacy.FX_MAP)
            mapping['checks']['check-a'] += ['tools/tests/a.c']
            result = fx.plan(files=['indirect.h'], map_=mapping)
            self.assertEqual(result[0], 'sel')
            self.assertIn('check-a', result[2])

    def test_manual_c_seed(self):
        with legacy.fixture(CS, files={'tools/tests/a.c': '#include "auto.h"\n',
                                     'tools/tests/auto.h': '#define N 1\n'}) as fx:
            mapping = copy.deepcopy(legacy.FX_MAP)
            mapping['checks']['check-a'] += ['tools/tests/a.c']
            result = fx.plan(files=['tools/tests/auto.h'], map_=mapping)
            self.assertEqual(result[0], 'sel')
            self.assertIn('check-a', result[2])

    def test_inline_c_header(self):
        script = 'FLAGS=["-Iinclude"]\nC = \'\'\'\n#include "auto.h"\n\'\'\'\n'
        with legacy.fixture(CS, files={'tools/tests/test_a.py': script,
                                     'include/auto.h': '#define N 1\n'}) as fx:
            result = fx.plan(files=['include/auto.h'])
            self.assertEqual(result[0], 'sel')
            self.assertIn('check-a', result[2])

    def test_build_routing(self):
        with legacy.fixture(CS, files={f'build/{n}.mk': 'CFLAGS += -O2\n' for n in ('kernel','libs','programs')}) as fx:
            mapping = copy.deepcopy(legacy.FX_MAP)
            mapping['broad'] = ['check-a']
            mapping['artifact_readers'] = ['check-b']
            self.assertEqual(fx.plan(files=['build/kernel.mk'], map_=mapping)[0], 'sel')
            self.assertEqual(set(fx.plan(files=['build/libs.mk'], map_=mapping)[2]), {'check-a','check-b'})

    def test_kapi_routing(self):
        with legacy.fixture(CS, files={'kapi/kapi_generated.c': ''}) as fx:
            mapping = copy.deepcopy(legacy.FX_MAP)
            mapping['checks']['check-b'] += ['kapi/**']
            self.assertEqual(fx.plan(files=['sdk/kapi.json'], map_=mapping)[2], ['check-b'])

    def test_kapi_generator_reader(self):
        with legacy.fixture(CS, files={'sdk/gen_kapi.py': ''}) as fx:
            mapping = copy.deepcopy(legacy.FX_MAP)
            mapping['checks']['check-b'] += ['sdk/gen_kapi.py']
            self.assertEqual(fx.plan(files=['sdk/kapi.json'], map_=mapping)[2], ['check-b'])

    def test_dialect_gate(self):
        self.assertFalse(CS.dialect_variants_needed(['docs/x.md', 'kernel/main.c']))
        for file in ['Makefile', 'build/kernel.mk', 'build/checks.d/check-a.mk',
                     'tools/check_c_dialect.py', 'tools/clang_ast/build.py',
                     'sdk/example/hello/Makefile', 'tools/tests/mutpar.py']:
            self.assertTrue(CS.dialect_variants_needed([file]), file)

    def test_shard_reorder_rejected(self):
        files = {'build/sdk.mk': 'CHECK_PAR_ORDER :=\n',
                 'build/checks.d/check-a.mk': 'CHECK_PAR_ORDER += 001:check-a\ncheck-a:\n'+legacy.A_LINE}
        with legacy.fixture(CS, files=files) as fx:
            fx.edit('build/checks.d/check-a.mk', '001:', '002:')
            mapping = copy.deepcopy(legacy.FX_MAP)
            mapping['full'].append('build/checks.d/*.mk')
            self.assertEqual(fx.plan(map_=mapping)[0], 'full')

    def test_header_leak_and_missing_source(self):
        with legacy.fixture(CS, files={'tools/tests/test_a.py': 'SRC="tools/tests/a.c"\n',
                                     'tools/tests/a.c': '#include "auto.h"\n',
                                     'tools/tests/auto.h': '#define N 1\n'}) as fx:
            mapping = copy.deepcopy(legacy.FX_MAP)
            mapping['checks'] = {k:v for k,v in mapping['checks'].items() if k in ['check-a','check-b']}
            mapping['checks']['check-a'] += ['tools/tests/a.c']
            old_root, old_trk, old_map = CS.ROOT, CS._TRACKED, CS.load_map
            try:
                CS.ROOT, CS._TRACKED, CS.load_map = fx.d, None, lambda: mapping
                import contextlib, io
                with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(CS.lint(), 0)
                    mapping['checks']['check-a'].remove('tools/tests/a.c')
                    self.assertEqual(CS.lint(), 1)
            finally:
                CS.ROOT, CS._TRACKED, CS.load_map = old_root, old_trk, old_map

    def test_routed_controls(self):
        controls = ['check-a:\n\techo override\n', 'check-a: MUT=\n',
                    '.ONESHELL:\n', 'export X=1\n', 'override X=1\n',
                    'CHECK_PAR_TARGETS += check-x\n', 'HOST32_RUNNERS=native\n',
                    '\tpython3 -B tools/tests/test_host32.py $(MUT)\n']
        for name in ('kernel', 'libs', 'programs'):
            path = 'build/' + name + '.mk'
            for control in controls:
                for deleting in (False, True):
                    with self.subTest(path=path, control=control, deleting=deleting):
                        with legacy.fixture(CS, files={path: 'CFLAGS += -O2\n' + (control if deleting else '')}) as fx:
                            fx.write(path, 'CFLAGS += -O2\n' + ('' if deleting else control))
                            self.assertEqual(fx.plan()[0], 'full')

    def test_routed_without_base(self):
        with legacy.fixture(CS) as fx:
            for name in ('kernel', 'libs', 'programs'):
                self.assertEqual(fx.plan(base=None, files=['build/'+name+'.mk'])[0], 'full')

    def test_main_commit_and_configured_base(self):
        for branch in ('main', 'release/test'):
            with self.subTest(branch=branch), legacy.fixture(CS) as fx:
                if branch != 'main':
                    legacy.git(fx.d, 'checkout', '-qb', branch)
                fx.write('tools/tests/test_a.py', '# committed change\n')
                legacy.git(fx.d, 'add', '.')
                legacy.git(fx.d, 'commit', '-qm', 'change')
                old_root, old_map = CS.ROOT, CS.load_map
                try:
                    CS.ROOT = fx.d
                    mapping = copy.deepcopy(legacy.FX_MAP)
                    mapping['base_refs'] = ['main', 'origin/main', branch]
                    CS.load_map = lambda: mapping
                    base, note = CS.default_base()
                    self.assertEqual(base, CS.git('rev-parse', 'HEAD~1')[0])
                    self.assertIn('HEAD~1', note)
                    committed, work = CS.changed_files(base)
                    self.assertEqual(committed, ['tools/tests/test_a.py'])
                    self.assertEqual(CS.plan(committed + work, base)[0], 'sel')
                finally:
                    CS.ROOT, CS.load_map = old_root, old_map

    def test_kapi_artifacts(self):
        with legacy.fixture(CS) as fx:
            mapping = copy.deepcopy(legacy.FX_MAP)
            mapping['artifact_readers'] = ['check-a']
            mapping['checks']['check-b'] += ['build/out/kernel.map']
            self.assertEqual(set(fx.plan(files=['sdk/kapi.json'], map_=mapping)[2]), {'check-a', 'check-b'})

    def test_each_generated_reader(self):
        for path in sorted(self.generated_outputs()):
            with self.subTest(path=path), legacy.fixture(CS) as fx:
                mapping = copy.deepcopy(legacy.FX_MAP)
                mapping['checks']['check-b'] += [path]
                self.assertIn('check-b', fx.plan(files=['sdk/kapi.json'], map_=mapping)[2])

    def generated_outputs(self):
        # Run the real generators in an empty disposable directory. Link scripts
        # are copied since gen_kapi rewrites their generation-reference sections.
        import tempfile, shutil
        with tempfile.TemporaryDirectory(prefix='checkinfra-kapi-') as tmp:
            root = pathlib.Path(tmp)
            for directory in ('sdk/include/os32', 'kapi', 'exec', 'sdk/link', 'build'):
                (root/directory).mkdir(parents=True, exist_ok=True)
            for path in ('sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/app_sys.ld',
                         'sdk/link/shlib.ld', 'build/os32.ld'):
                shutil.copyfile(legacy.ROOT/path, root/path)
            before = {p.relative_to(root).as_posix(): p.stat().st_mtime_ns for p in root.rglob('*') if p.is_file()}
            for script in ('sdk/gen_kapi.py', 'sdk/kapi_rust_gen.py'):
                proc = subprocess.run([sys.executable, '-B', str(legacy.ROOT/script)], cwd=root,
                                      capture_output=True, text=True)
                self.assertEqual(proc.returncode, 0, proc.stderr)
            outputs = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()
                       and before.get(p.relative_to(root).as_posix()) != p.stat().st_mtime_ns}
            return outputs

    def test_generated_outputs_match_generators(self):
        self.assertEqual(set(getattr(CS, 'KAPI_GENERATED', ())), self.generated_outputs())

    def test_check_rules_in_shards(self):
        import re
        for name in ("kernel", "libs", "programs"):
            self.assertIsNone(re.search(r"^check-(?:ne2000-ring|shlib):", (legacy.ROOT/("build/"+name+".mk")).read_text(), re.M))
        for target in ("check-ne2000-ring", "check-shlib"):
            self.assertIn(target + ":\n", (legacy.ROOT/("build/checks.d/"+target+".mk")).read_text())

    def test_real_rust_readers(self):
        from unittest import mock
        mapping = CS.load_map()
        for path in ('sdk/rust/os32api/src/lib.rs', 'sdk/rust/os32api/src/kapi_generated.rs', 'sdk/kapi.json'):
            with self.subTest(path=path), mock.patch.object(CS, 'effective_checks', lambda m, r: m['checks']), \
                 mock.patch.object(CS, 'tracked', return_value=self.generated_outputs()):
                result = CS.plan([path])
                self.assertIn('check-edit-doc-host', result[2])
                self.assertIn('check-tools-host', result[2])

    def test_routed_lint_leak(self):
        import contextlib, io
        for name in ('kernel', 'libs', 'programs'):
            path = 'build/' + name + '.mk'
            with self.subTest(path=path), legacy.fixture(CS, files={path: '# input\n',
                    'tools/tests/test_a.py': 'INPUT = "' + path + '"\n'}) as fx:
                mapping = copy.deepcopy(legacy.FX_MAP)
                mapping['checks'] = {k:v for k,v in mapping['checks'].items() if k in ('check-a','check-b')}
                old_root, old_trk, old_map = CS.ROOT, CS._TRACKED, CS.load_map
                try:
                    CS.ROOT, CS._TRACKED, CS.load_map = fx.d, None, lambda: mapping
                    err = io.StringIO()
                    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(err):
                        self.assertEqual(CS.lint(), 1)
                        self.assertIn(path + ' が glob に入っていない', err.getvalue())
                        mapping['checks']['check-a'].append(path)
                        self.assertEqual(CS.lint(), 0)
                finally:
                    CS.ROOT, CS._TRACKED, CS.load_map = old_root, old_trk, old_map

    def test_db_mutants_leave_original_untouched(self):
        import contextlib, io, tempfile, types
        from unittest import mock
        path = legacy.ROOT / 'tools/tests/test_db_errstr.py'
        db = types.ModuleType('db_isolation_probe')
        db.__file__ = str(path)
        code = subprocess.check_output(['git', 'show', 'HEAD:tools/tests/test_db_errstr.py'], text=True) if BEFORE else path.read_text()
        exec(compile(code, str(path), 'exec'), db.__dict__)
        original = (legacy.ROOT / 'kapi/kapi_db.c').read_text()
        with tempfile.TemporaryDirectory(prefix='checkinfra-db-') as tmp:
            root = pathlib.Path(tmp)/'base'
            (root/'kapi').mkdir(parents=True)
            (root/'tools/tests').mkdir(parents=True)
            (root/'kapi/kapi_db.c').write_text(original)
            (root/'tools/tests/db_errstr_host.c').write_text('/* probe */')
            db.ROOT = root
            work = pathlib.Path(tmp)/'work'
            work.mkdir()
            calls = []
            def build(tmp, tag, sqlite_obj, san, root=None):
                self.assertEqual((db.ROOT/'kapi/kapi_db.c').read_text(), original)
                self.assertIsNotNone(root)
                self.assertNotEqual(root, db.ROOT)
                self.assertFalse((root/'kapi/kapi_db.c').is_symlink())
                calls.append(tag)
                return tag
            def cases(exe, cases, root=None):
                return 0 if exe == 'control' else 1
            with mock.patch.object(db, 'build', build), mock.patch.object(db, 'run_cases', cases), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(db.run_mutations(work, 'unused', []), 0)
            self.assertEqual(len(calls), 9)
            self.assertEqual((root/'kapi/kapi_db.c').read_text(), original)

    def test_mutate_switches(self):
        import os
        env = dict(os.environ, NP21W_DIR='/dev/null', PYTHONPATH='')
        # 外側の make (check-changed の MUTATE=sel / MUTATE_TARGETS) から継ぐ値を落とす。
        for k in ('MAKEFLAGS', 'MFLAGS', 'MAKELEVEL', 'MAKEOVERRIDES', 'MUT', 'MUTATE', 'MUTATE_TARGETS'):
            env.pop(k, None)
        for mutate, expected in [('0', 0), ('1', 2)]:
            proc = subprocess.run(['make', '-n', 'check-ring3-guard-host', 'check-db-errstr-host',
                                   'MUTATE='+mutate, 'MUTATE_TARGETS=', 'NP21W_DIR=/dev/null'],
                                  cwd=legacy.ROOT, env=env, stdin=subprocess.DEVNULL,
                                  text=True, capture_output=True)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(proc.stdout.count('--mutate'), expected)

    def test_generated_inventory_freshness(self):
        import tempfile
        with tempfile.TemporaryDirectory(prefix='checkinfra-gen-') as tmp:
            edits = {'docs/TESTS.md': (legacy.ROOT / 'docs/TESTS.md').read_text() + 'stale\n',
                     'docs/08_build.md': (legacy.ROOT / 'docs/08_build.md').read_text().replace(
                         '<!-- generated:host32 -->', '<!-- generated:host32 -->\nstale')}
            root = legacy.mutpar.mutant_tree(legacy.ROOT, pathlib.Path(tmp)/'tree', edits,
                       real={'tools/gen_tests_inventory.py', 'tools/check_select.py'})
            def run(flag):
                return subprocess.run([sys.executable, '-B', str(root/'tools/gen_tests_inventory.py'), flag],
                                      capture_output=True, text=True)
            self.assertEqual(run('--check').returncode, 1)
            self.assertEqual(run('--write').returncode, 0)
            result = run('--check')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


MUTATIONS = [
    ('if not routed_build_safe(base, f):', 'if False:'),
    ('("+ ", "- ")', '("+ ",)'),
    ('            hit |= artifact_targets\n', '            pass\n'),
    ('if branch in branches:', 'if False:'),
    ('f not in ROUTED_BUILD and matches(f, full)', 'matches(f, full)'),
    ('"sdk/gen_kapi.py"', '"tools/gen_kapi.py"'),
    ('    for source in seeds:', '    for source in []:'),
    ('        for inc in C_INC.findall(text):\n            for base', '        for inc in []:\n            for base'),
    ('if branch in branches:', 'if True:'),
    (' or TPL_HOST32_RE.match(line)', ''),
    ('if not in_tail or mp.group(1) != cur_new or not picked.get(cur_new):', 'if False:'),
    ('if f.endswith(".h"))\n            for t, g', 'if False)\n            for t, g'),
    ('if f in ROUTED_BUILD:', 'if False:'),
    ('hit |= broad | artifact_targets', 'hit |= broad'),
    ('if f == "sdk/kapi.json":', 'if False:'),
    ('return any(matches(f, patterns) for f in files)', 'return True'),
    ('return any(matches(f, patterns) for f in files)', 'return False'),
    ('return narrow_shards(bt, wt, mk_hits, m_checks)', 'raise Reject("no shards")'),
]


def main():
    global CS
    mutate = '--mutate' in sys.argv
    if mutate:
        sys.argv.remove('--mutate')
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(CheckInfra)
    result = unittest.TextTestRunner().run(suite)
    if not result.wasSuccessful():
        return 1
    if mutate:
        import contextlib, io
        source = legacy.SRC.read_text()
        for i, (old, new) in enumerate(MUTATIONS, 1):
            assert source.count(old) == 1, (i, old)
            CS = legacy.load(source.replace(old, new))
            suite = unittest.defaultTestLoader.loadTestsFromTestCase(CheckInfra)
            with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                result = unittest.TextTestRunner(stream=io.StringIO()).run(suite)
            if result.wasSuccessful():
                print('MUTATION %d GREEN' % i)
                return 1
            print('MUTATION %d RED' % i)
    return 0


if __name__ == '__main__':
    sys.exit(main())
