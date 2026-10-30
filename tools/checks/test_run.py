#!/usr/bin/env python3
"""Small, isolated fixtures for the declaration runner (no production checks)."""
import contextlib
from dataclasses import replace
import importlib.util
import io
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('checks_run', Path(__file__).with_name('run.py'))
R = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = R
SPEC.loader.exec_module(R)


class RunnerTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='runner-fixture-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.git('init', '-q', '-b', 'main')
        self.git('config', 'user.name', 'Fixture')
        self.git('config', 'user.email', 'fixture@example.invalid')
        self.write('.gitignore', '__pycache__/\nbuild/out/\n')
        self.write('drivers/video/a.c', 'int a;\n')
        self.commit()
        self.env = {k: v for k, v in os.environ.items() if k not in (
            'MUTATE', 'MUTATE_TARGETS', 'MUT', 'MUTS', 'CHECK_TIME_CHILD', 'CC_FILES_JSON',
            'CC_TIER', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL', 'MAKEOVERRIDES')}
        self.env['HOST32_RUNNERS'] = 'native qemu'

    def write(self, path, text):
        dest = self.root / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text)
        return dest

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args], text=True).strip()

    def commit(self):
        self.git('add', '.')
        self.git('commit', '-qm', 'fixture')

    def check(self, name='a', covers=None, **kwargs):
        path = 'tools/checks/' + name + '.py'
        data = dict(name=name, covers=covers if covers is not None else ['drivers/video/a.c'], budget=60)
        body = kwargs.pop('body', '    return 0\n')
        prefix = kwargs.pop('prefix', '')
        data.update(kwargs)
        self.write(path, 'CHECK = dict(' + ', '.join(k + '=' + repr(v) for k, v in data.items()) + ')\n'
                   + prefix + '\ndef run(ctx):\n' + body)
        return name

    def registry(self):
        return R.discover(self.root)

    def plan(self, *files, floor=0):
        return R.plan(self.root, self.registry(), files, tier_floor=floor)

    def cli(self, *args, env=None):
        out, err = io.StringIO(), io.StringIO()
        with patch.object(R, 'ROOT', self.root), patch.dict(os.environ, env or self.env, clear=True), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = R.main(list(args))
        return rc, out.getvalue(), err.getvalue()

    def test_ast_list_and_json_do_not_import_or_extract(self):
        self.check('z', budget=8, prefix='raise RuntimeError("imported")\n')
        self.check('a', budget=8)
        self.check('slow', budget=90)
        self.write('tools/checks/helper.py', 'raise RuntimeError("helper imported")\n')
        with patch.object(R, 'extract', side_effect=AssertionError('dependency scan')):
            rc, out, _ = self.cli('--list')
            self.assertEqual((rc, out.splitlines()), (0, ['check-slow', 'check-a', 'check-z']))
            rc, out, _ = self.cli('--list', '--json')
            row = json.loads(out)[0]
            self.assertEqual((rc, row['name'], row['target'], row['mutate_tier']), (0, 'slow', 'check-slow', 2))
        self.assertEqual(self.cli('--lint')[0], 0, self.cli('--lint')[2])

    def test_types_duplicates_and_outside(self):
        for name in R.OUTSIDE:
            self.check(name)
        self.assertEqual(self.registry(), {})
        path = self.write('tools/tests/test_bad.py', 'CHECK = dict(name="bad", covers=[], budget=1)\ndef run(ctx): return 0\n')
        for value in ('dict(name="bad", covers=[], budget=True)',
                      'dict(name="bad", covers=[], budget=1, runners=[])',
                      'dict(name="bad", covers=[], budget=1, tier=True)',
                      'dict(name="bad", covers=(), budget=1)',
                      'dict(name="bad", covers=[], budget=1, needs=["all"])',
                      'dict(name="bad", covers=[], budget=1, mutate_tier=9)',
                      'dict(name="bad", covers=[], budget=compute())'):
            with self.subTest(value=value):
                path.write_text('CHECK = ' + value + '\ndef run(ctx): return 0\n')
                self.assertEqual(self.cli('--list')[0], 2)
                self.assertEqual(self.cli('--lint')[0], 1)
        path.write_text('CHECK = dict(name="a", covers=[], budget=1)\ndef run(ctx): return 0\n')
        self.check()
        self.assertIn('duplicate', self.cli('--list')[2])

    def test_leaf_floor_and_hit_tiers(self):
        for tier in (1, 2, 3, 4):
            self.check('t' + str(tier), tier=tier)
        for name in R.STATIC_LANDING:
            self.check(name, covers=['other/input'])
        direct = self.plan('drivers/video/a.c')
        self.assertEqual((direct.tier, direct.mutations), (1, ()))
        self.assertTrue({'t1', 't2', 't3'} <= set(direct.stage))
        self.assertNotIn('t4', direct.stage)
        self.assertTrue(R.STATIC_DIRECT <= set(direct.stage))
        self.assertNotIn('privileged', direct.stage)
        landing = self.plan('drivers/video/a.c', floor=2)
        self.assertEqual(landing.tier, 2)
        self.assertEqual(set(landing.mutations), {'t1', 't2', 't3'})
        self.assertTrue(R.STATIC_LANDING <= set(landing.stage))

    def test_function_layer_and_unmatched(self):
        self.check('a', covers=['drivers/video/**'])
        self.check('shared', covers=['elsewhere.c'], tier=3)
        self.check('other', covers=['elsewhere.c'])
        self.check('stable', covers=['elsewhere.c'], tier=4)
        self.assertEqual(self.plan('drivers/video/a.c', 'drivers/video/b.c').tier, 2)
        self.assertEqual(self.plan('drivers/video/api.h').tier, 2)
        for files in [('drivers/video/a.c', 'drivers/net/a.c'), ('include/x.h',), ('mm/a.c',), ('unknown.c',)]:
            with self.subTest(files=files):
                result = self.plan(*files)
                self.assertEqual(result.tier, 3)
                self.assertIn('shared', result.stage)
                self.assertNotIn('stable', result.stage)
        result = self.plan('unknown.c')
        self.assertEqual(result.unmatched, {'unknown.c'})
        self.assertIn('other', result.stage)
        self.assertEqual(result.mutations, ())
        self.assertEqual(self.plan('drivers/video/new.c').unmatched, set())
        result = self.plan('drivers/video/a.c', 'else/missing.c')
        self.assertEqual(result.mutations, ('a',))

    def test_shared_and_mutate_tier(self):
        self.check('a', mutate_tier=4)
        self.check('shared', covers=['missing'], tier=3)
        self.check('doc', covers=['missing'], tier=0)
        result = self.plan('drivers/video/a.c', floor=3)
        self.assertEqual(set(result.stage), {'a', 'shared', 'doc'})
        self.assertEqual(result.mutations, ())
        self.assertEqual(set(self.plan('drivers/video/a.c', floor=4).mutations), {'a', 'shared', 'doc'})

    def test_fast_docs_full_and_three_sets(self):
        self.check('a', covers=['docs/*.md'])
        self.check('stable', tier=4)
        for name in R.DOCS_ALWAYS:
            self.check(name, covers=['nonmatching'])
        fast = self.plan()
        self.assertEqual((fast.mode, fast.tier, fast.mutations), ('fast', 4, ()))
        self.assertEqual(self.plan('.claude/a').mode, 'fast')
        docs = self.plan('docs/note.md', floor=2)
        self.assertEqual((docs.tier, docs.mode), (0, 'docs'))
        self.assertEqual(set(docs.stage), R.DOCS_ALWAYS | {'a'})
        self.assertEqual(docs.mutations, docs.stage)
        for file in ('Makefile', 'build/kernel.mk', 'build/checks.d/a.mk', 'sdk/kapi.json',
                     'sdk/gen_kapi.py', 'sdk/link/generations.ld', 'sdk/rust/abc/Cargo.toml',
                     'tools/checks/run.py'):
            with self.subTest(file=file):
                result = self.plan(file)
                self.assertEqual((result.tier, result.mode, result.mutations), (4, 'full', result.stage))
                self.assertEqual(result.full_hits, {file} if R.matches(file, R.FULL + R.BUILD_CONFIG_GLOBS) else set())
        own = self.plan('tools/checks/a.py')
        self.assertEqual((own.tier, own.hit), (1, {'a'}))

    def test_notest_empty_and_actual_hit(self):
        self.check()
        result = self.plan('userland/cmds/foo.c', floor=2)
        self.assertEqual((result.tier, result.mode, result.stage, result.needs, result.mutations),
                         (0, 'sel', (), (), ()))
        self.assertIn('notest', R.emit_shell(result))
        self.assertIn('userland/cmds/stat.c', self.plan('userland/cmds/stat.c').unmatched)
        self.assertIn('docs/manpages/hsync.1', self.plan('docs/manpages/hsync.1').unmatched)
        self.check('reader', covers=['userland/cmds/foo.c'])
        self.assertEqual(self.plan('userland/cmds/foo.c', floor=2).mutations, ('reader',))

    def test_kapi_reader_routing(self):
        self.check('reader', covers=['build/out/kernel.map'], needs=['make:all'])
        self.check('generator-reader', covers=['sdk/include/os32/os32_kapi_slots.h'])
        self.check('kapi-version', covers=['none'])
        result = self.plan('sdk/kapi.json')
        self.assertEqual(result.hit, {'reader', 'generator-reader', 'kapi-version'})
        self.assertEqual(result.full_hits, set())
        self.assertEqual(result.needs, ('all',))

    def test_dependency_closure_static_and_compiler(self):
        self.check(covers=['drivers/video/a.c'], prefix='import helper\n')
        self.write('tools/checks/helper.py', 'INC = ["-Iinclude"]\nSRC = "tools/tests/probe.c"\n')
        self.write('tools/tests/probe.c', '#include "shared.h"\n')
        self.write('drivers/video/a.c', '#include "own.h"\n#if 0\n#include "inactive.h"\n#endif\n#include <active.h>\n')
        self.write('drivers/video/own.h', '#include "next.h"\n')
        for p in ('drivers/video/next.h', 'drivers/video/inactive.h', 'include/active.h', 'include/shared.h'):
            self.write(p, '/* header */\n')
        inputs = R.inputs(self.root, self.registry()['a'])
        self.assertTrue({'include/active.h', 'include/shared.h', 'drivers/video/inactive.h',
                         'drivers/video/next.h'} <= inputs)
        for path in ('include/active.h', 'include/shared.h', 'drivers/video/inactive.h'):
            self.assertIn('a', self.plan(path).hit)
        with patch.object(R, 'compiler_headers', return_value=frozenset()):
            self.assertIn('a', self.plan('drivers/video/inactive.h').hit)
        self.write('drivers/video/a.c', '#include "new.h"\n')
        self.write('drivers/video/new.h', '/* new */\n')
        self.assertIn('a', self.plan('drivers/video/new.h').hit)
        with tempfile.TemporaryDirectory(prefix='runner-other-') as other:
            root = Path(other)
            subprocess.run(['git', 'init', '-q', other], check=True)
            (root / 'probe.c').write_text('#include <missing.h>\n')
            self.assertEqual(R.compiler_headers(other, ('probe.c',), ()), set())
        with patch.object(R.subprocess, 'run', side_effect=OSError('no gcc')):
            R.compiler_headers.cache_clear()
            with self.assertRaises(OSError):
                R.compiler_headers(str(self.root), ('drivers/video/a.c',), ())

    def test_dependency_seeds_exclude_globs_and_direct_headers(self):
        self.write('drivers/video/a.c', '#include "glob_only.h"\n')
        self.write('drivers/video/glob_only.h', '/* glob dependency */\n')
        self.write('include/direct.h', '#include "transitive.h"\n')
        self.write('include/transitive.h', '/* header dependency */\n')
        self.write('tools/tests/recipe.c', '#include "recipe.h"\n')
        self.write('tools/tests/recipe.h', '/* recipe dependency */\n')
        self.check(covers=['drivers/video/*.c', 'include/direct.h'],
                   body='    source = "tools/tests/recipe.c"\n    return 0\n')
        sources = []
        with patch.object(R, 'compiler_headers', side_effect=lambda root, src, inc: sources.extend(src) or frozenset()):
            self.assertNotIn('a', self.plan('drivers/video/glob_only.h').hit)
            self.assertNotIn('a', self.plan('include/transitive.h').hit)
            self.assertIn('a', self.plan('tools/tests/recipe.h').hit)
        self.assertEqual(set(sources), {'tools/tests/recipe.c'})
        self.assertIn('a', self.plan('drivers/video/new.c').hit)
        self.assertIn('a', self.plan('include/direct.h').hit)

    def test_kapi_artifact_reader_without_output_covers(self):
        self.check('build-no-deploy', covers=['tools/tests/test_build_no_deploy.py'], needs=['make:all'])
        self.write('tools/tests/test_build_no_deploy.py', '# build definition reader\n')
        self.check('other', covers=['drivers/video/a.c'], needs=['make:kernel'])
        result = self.plan('sdk/kapi.json')
        self.assertEqual((result.hit, result.unmatched, result.full_hits),
                         ({'build-no-deploy'}, set(), set()))

    def test_routed_build_whitespace_forces_tier_after_three_sets(self):
        self.write('build/kernel.mk', 'kernel: source.c\n')
        self.commit()
        base = self.git('rev-parse', 'HEAD')
        self.write('build/kernel.mk', 'kernel: source.c\n\n')
        self.check('scanner', covers=['drivers/**'])
        self.check('reader', covers=['tools/tests/reader.py'], needs=['make:all'])
        self.check('stable', covers=['unrelated'], tier=4)
        result = R.plan(self.root, self.registry(), ('build/kernel.mk',), base=base)
        self.assertEqual((result.hit, result.unmatched, result.full_hits),
                         ({'scanner', 'reader'}, set(), set()))
        self.assertEqual((result.tier, result.mode), (4, 'full'))
        self.assertIn('stable', result.stage)

    def test_lint_inputs_artifacts_and_dynamic_generated_audit(self):
        self.check(covers=['absent.c'])
        self.assertIn('missing input', self.cli('--lint')[2])
        self.check(covers=['drivers/video/a.c', 'build/out/*.bin'])
        self.assertIn('make needs', self.cli('--lint')[2])
        self.check(covers=['drivers/video/a.c', 'build/out/*.bin'], needs=['make:all'])
        self.assertEqual(self.cli('--lint')[0], 0, self.cli('--lint')[2])
        self.check(covers=['drivers/video/a.c'], prefix='INPUT = "tools/tests/helper.py"\n')
        self.write('tools/tests/helper.py', 'P = "build/sdk/" + name + ".bin"\n')
        err = self.cli('--lint')[2]
        self.assertIn('uncovered input tools/tests/helper.py', err)
        self.assertIn('artifact', err)
        self.check(covers=['drivers/video/a.c', 'tools/tests/helper.py', 'build/sdk/**'], needs=['make:all'])
        self.assertEqual(self.cli('--lint')[0], 0, self.cli('--lint')[2])
        self.write('tools/tests/helper.py', 'P = "sdk/include/os32/" + name + ".h"\n')
        err = self.cli('--lint')[2]
        self.assertIn('uncovered generated input sdk/include/os32/os32_kapi_slots.h', err)
        self.assertIn('uncovered generated input sdk/gen_kapi.py', err)

    def test_git_base_work_untracked_deleted_and_rename(self):
        initial = self.git('rev-parse', 'HEAD')
        self.assertEqual(R.default_base(self.root), initial)
        self.write('drivers/video/a.c', 'int b;\n')
        self.commit()
        self.assertEqual(R.default_base(self.root), initial)
        self.git('switch', '-qc', 'work')
        self.assertEqual(R.default_base(self.root), self.git('rev-parse', 'HEAD'))
        self.git('mv', 'drivers/video/a.c', 'drivers/video/new.c')
        self.write('drivers/video/loose.c', 'int x;\n')
        self.assertEqual(set(R.changed_files(self.root, initial)),
                         {'drivers/video/a.c', 'drivers/video/new.c', 'drivers/video/loose.c'})
        self.check()
        rc, out, _ = self.cli('--select', '--files')
        self.assertEqual((rc, self.shell(out)['CC_MODE']), (0, 'fast'))
        rc, out, _ = self.cli('--select', '--base', initial)
        self.assertIn('drivers/video/new.c', json.loads(self.shell(out)['CC_FILES_JSON']))

    @staticmethod
    def shell(text):
        return {line.split('=', 1)[0]: shlex.split(line.split('=', 1)[1])[0] for line in text.splitlines()}

    def test_shell_roundtrip_preserves_paths_and_no_execution(self):
        self.check(needs=['make:kernel'])
        files = ("drivers/video/a.c", "a b'$(touch bad).c")
        result = self.plan(*files)
        script = R.emit_shell(result) + '\npython3 -c "import os; print(os.environ[\'CC_FILES_JSON\'])"\n'
        proc = subprocess.run(['sh', '-c', 'set -a\n' + script], cwd=self.root, capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0)
        self.assertEqual(json.loads(proc.stdout), list(files))
        self.assertFalse((self.root / 'bad').exists())
        with patch.object(R, 'run_one', side_effect=AssertionError('execution')):
            self.assertEqual(self.cli('--select', '--files', 'drivers/video/a.c')[0], 0)

    def test_make_prepare_once_closure_and_child(self):
        self.check('prereq', needs=['make:shared'], budget=80)
        self.check('ver-info-host', needs=['check:prereq', 'make:shared', 'make:second'])
        self.write('Makefile', '.PHONY: shared second\nshared second:\n\t@echo $@ >> prepared\n')
        calls = []
        with patch.object(R, 'run_one', side_effect=lambda check, ctx: calls.append(check.name) or 0):
            self.assertEqual(self.cli('--run', 'check-ver-info-host', 'prereq')[0], 0)
        self.assertEqual(calls, ['prereq', 'ver-info-host'])
        self.assertEqual((self.root / 'prepared').read_text().splitlines(), ['second', 'shared'])
        calls.clear()
        with patch.object(R, 'run_one', side_effect=lambda check, ctx: calls.append(check.name) or 0):
            self.assertEqual(self.cli('--run', 'ver-info-host', env=dict(self.env, CHECK_TIME_CHILD='1'))[0], 0)
        self.assertEqual(calls, ['ver-info-host'])
        self.assertEqual(len((self.root / 'prepared').read_text().splitlines()), 2)
        self.write('Makefile', 'shared second:\n\t@exit 7\n')
        with patch.object(R, 'run_one', side_effect=AssertionError('after prepare failure')):
            self.assertNotEqual(self.cli('--run', 'ver-info-host')[0], 0)
        self.assertEqual(self.cli('--run', 'unknown')[0], 2)

    def test_context_mutation_files_and_make_variables(self):
        self.check()
        for mutate, targets, want in [('0', '', False), ('1', '', True), ('sel', 'check-a', True), ('sel', '', False)]:
            for files in (None, (), ('drivers/video/a.h',)):
                with self.subTest(mutate=mutate, files=files):
                    contexts = []
                    env = dict(self.env, MUTATE=mutate, MUTATE_TARGETS=targets, BUILD_OUT='private/output')
                    if files is not None:
                        env['CC_FILES_JSON'] = json.dumps(files)
                    with patch.object(R, 'run_one', side_effect=lambda check, ctx: contexts.append(ctx) or 0):
                        self.assertEqual(self.cli('--run', 'a', env=env)[0], 0)
                    ctx = contexts[0]
                    self.assertEqual((ctx.files, ctx.mutate, ctx.runners), (files, want, ('native', 'qemu')))
                    self.assertEqual((ctx.make_vars['MUT'], ctx.make_vars['MUTS']),
                                     ('--mutate', '--mutants') if want else ('', ''))
                    self.assertEqual(ctx.make_vars['BUILD_OUT'], 'private/output')
        contexts = []
        with patch.object(R, 'run_one', side_effect=lambda check, ctx: contexts.append(ctx) or 0):
            self.cli('--run', 'a', '--files', 'a.h', env=dict(self.env, CC_TIER='4'))
        self.assertIsNone(contexts[0].files)

    def test_run_recipe_host32_failure_and_budget(self):
        self.check(body='    import subprocess, sys\n'
                   '    for index, runner in enumerate(ctx.runners):\n'
                   '        args = [sys.executable, "probe.py", runner]\n'
                   '        if ctx.mutate and index == 0: args.append(ctx.make_vars["MUT"])\n'
                   '        rc = subprocess.run(args).returncode\n'
                   '        if rc: return rc\n'
                   '    return 0\n', budget=0)
        self.write('probe.py', 'import sys\nwith open("calls", "a") as f: f.write(" ".join(sys.argv[1:]) + "\\n")\n')
        rc, _, err = self.cli('--run', 'a')
        self.assertEqual(rc, 0)
        self.assertEqual((self.root / 'calls').read_text().splitlines(), ['native --mutate', 'qemu'])
        rc, _, err = self.cli('--run', 'a', env=dict(self.env, MUTATE='0'))
        self.assertEqual(rc, 0)
        self.assertIn('budget', err)
        self.check('stop', budget=200, body='    return 7\n')
        (self.root / 'calls').unlink()
        self.assertEqual(self.cli('--run', 'a', 'stop')[0], 7)
        self.assertFalse((self.root / 'calls').exists())
        self.assertEqual(Path.cwd(), Path(__file__).resolve().parents[2])

    def test_dialect_gate(self):
        self.assertFalse(R.dialect_variants_needed(()))
        self.assertFalse(R.dialect_variants_needed(('kernel/ime/a.c',)))
        self.assertTrue(R.dialect_variants_needed(('tools/checks/run.py',)))
        self.assertTrue(R.dialect_variants_needed(('build/kernel.mk',)))


if __name__ == '__main__':
    unittest.main()
