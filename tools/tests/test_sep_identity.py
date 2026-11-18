#!/usr/bin/env python3
"""tools/ci/sep_identity.py: 宣言の読み取り・移したソースの読み替え・.o の比較・結論 (像は組まない)。"""
CHECK = dict(name='sep-identity-host', tier=1, needs=[], budget=5,
    covers=['tools/ci/sep_identity.py', 'tools/tests/test_sep_identity.py', 'tools/build_compare.py',
            'tools/gen_build_id.py'],
    basis=['SEP_IDENTITY_DESIGN §2', 'SEP_IDENTITY_DESIGN §3'],
)
import argparse
import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'tools/ci/sep_identity.py'


def load(path):
    spec = importlib.util.spec_from_file_location('sep_identity_under_test', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def suite(tool):
    class IdentityTest(unittest.TestCase):
        def test_declared(self):
            self.assertEqual(tool.declared('subject\n\nbody\n'), set())
            self.assertEqual(tool.declared('a\n\nSep-Changes: ppc\n'), {'ppc'})
            self.assertEqual(tool.declared('a\n\n  Sep-Changes:  x86 ppc \n\nb\n\nSep-Changes: x86\n'), {'x86', 'ppc'})
            # 文の途中の語は宣言ではない。
            self.assertEqual(tool.declared('この段は Sep-Changes: ppc\n'), set())
            with self.assertRaises(ValueError):
                tool.declared('Sep-Changes: arm\n')

        def test_renames(self):
            status = ('M\tbuild/select.mk\n'
                      'R100\tarch/ppc/moved_hook.c\texec/absent/moved_hook.c\n'
                      'R095\tarch/ppc/a.S\tarch/ppc/b.S\n'
                      'C100\tlib/a.c\tlib/copy.c\n'
                      'A\texec/exec_hooks.h\nD\tarch/x86/gone_hook.h\n')
            self.assertEqual(tool.renames(status), {'exec/absent/moved_hook.c': 'arch/ppc/moved_hook.c',
                                                    'arch/ppc/b.S': 'arch/ppc/a.S'})

        def test_object_key(self):
            out = 'build/out/ppc-kboot/'
            self.assertEqual(tool.object_key(out + 'mm/paging.o'), ('', 'mm/paging'))
            self.assertEqual(tool.object_key(out + 'link-sh/exec/absent/moved_hook.o'),
                             ('link-sh', 'exec/absent/moved_hook'))
            self.assertEqual(tool.object_key(out + 'neg1/lib/lz4.o'), ('neg1', 'lib/lz4'))
            # 核の像は根の直下に出る: ソースの最上位が像の名前と同じ綴りで始まらない限り像は空。
            self.assertEqual(tool.object_key(out + 'kernel/sys.o'), ('', 'kernel/sys'))

        def test_compare_objects(self):
            base = {('', 'mm/paging'): 'a', ('sh', 'arch/ppc/moved_hook'): 'b', ('sh', 'exec/exec'): 'c',
                    ('sh', 'gone/old'): 'd'}
            head = {('', 'mm/paging'): 'a', ('sh', 'exec/absent/moved_hook'): 'b', ('sh', 'exec/exec'): 'x',
                    ('sh', 'new/file'): 'e'}
            moved = {'exec/absent/moved_hook.c': 'arch/ppc/moved_hook.c'}
            self.assertEqual(tool.compare_objects(base, head, moved),
                             (['sh/exec/exec'], ['sh/gone/old'], ['sh/new/file']))
            # 読み替えが無ければ、移した .o は「基点にだけ」と「HEAD にだけ」に出る。
            self.assertEqual(tool.compare_objects(base, head, {})[1:],
                             (['sh/arch/ppc/moved_hook', 'sh/gone/old'], ['sh/exec/absent/moved_hook', 'sh/new/file']))
            self.assertEqual(tool.compare_objects(base, dict(base), {}), ([], [], []))

        def test_moved_object_pairs(self):
            moved = {'platform/pc98/moved_main.c': 'init/moved_main.c', 'platform/pc98/note.md': 'init/note.md'}
            diff = ['build/out/obj/init/moved_main.o', 'build/out/obj/platform/pc98/moved_main.o',
                    'build/out/r1/obj/init/moved_main.o', 'build/out/r1/obj/platform/pc98/moved_main.o',
                    'build/out/kernel.bin', 'build/out/obj/mm/paging.o']
            pairs, rest = tool.moved_object_pairs(diff, moved)
            self.assertEqual(pairs, [('build/out/obj/init/moved_main.o', 'build/out/obj/platform/pc98/moved_main.o'),
                                     ('build/out/r1/obj/init/moved_main.o', 'build/out/r1/obj/platform/pc98/moved_main.o')])
            # 移していない物の違いと、出荷する生成物の違いは残る。
            self.assertEqual(rest, ['build/out/kernel.bin', 'build/out/obj/mm/paging.o'])
            # 基点の側の .o が違いに出ていなければ対にしない (新しく増えただけの .o は違いのまま)。
            self.assertEqual(tool.moved_object_pairs(['build/out/obj/platform/pc98/moved_main.o'], moved),
                             ([], ['build/out/obj/platform/pc98/moved_main.o']))

        def test_hash_objects_skips_volatile(self):
            with tempfile.TemporaryDirectory(prefix='os32-sep-identity-') as directory:
                tree = Path(directory)
                for name, data in (('sh/root_image_blob.o', b'iso'), ('sh/mm/paging.o', b'code'),
                                   ('mm/paging.o', b'code')):
                    path = tree / 'build/out/ppc-kboot' / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
                found = tool.hash_objects(tree)
            self.assertEqual(sorted(found), [('', 'mm/paging'), ('sh', 'mm/paging')])
            self.assertEqual(found[('', 'mm/paging')], found[('sh', 'mm/paging')])

        def verdict(self, *args):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                failed = tool.verdict(*args)
            return failed, out.getvalue()

        def test_verdict(self):
            self.assertEqual(self.verdict('x86', True, set(), [])[0], False)
            failed, text = self.verdict('x86', True, {'x86'}, [])
            self.assertFalse(failed)
            self.assertIn('同じだった', text)
            failed, text = self.verdict('ppc', False, set(), ['違う: sh/exec/exec'])
            self.assertTrue(failed)
            self.assertIn('FAIL ppc', text)
            self.assertIn('違う: sh/exec/exec', text)
            # 宣言は対象ごと: x86 の宣言は ppc の違いを通さない。
            self.assertTrue(self.verdict('ppc', False, {'x86'}, ['違う: a'])[0])
            failed, text = self.verdict('ppc', False, {'ppc'}, ['違う: a'])
            self.assertFalse(failed)
            self.assertIn('基点と違う', text)
            self.assertIn('違う: a', text)

    return unittest.defaultTestLoader.loadTestsFromTestCase(IdentityTest)


MUTANTS = [
    ("if word not in TARGETS:", "if False:"),
    ("re.fullmatch(r'Sep-Changes:\\s*(.*)', line.strip())", "re.search(r'Sep-Changes:\\s*(.*)', line.strip())"),
    ("if len(parts) == 3 and parts[0].startswith('R'):", "if len(parts) == 3:"),
    ("if rest[0] in ('link', 'link-sh', 'sh', 'shell') or rest[0].startswith('neg'):", "if False:"),
    ("mapped[(image, stems.get(stem, stem))] = ((image, stem), digest)", "mapped[(image, stem)] = ((image, stem), digest)"),
    ("if key in base and base[key] != mapped[key][1])", "if False)"),
    ("for key in base if key not in mapped)", "for key in base if False)"),
    ("for key in mapped if key not in base)", "for key in mapped if False)"),
    ("if path.name in VOLATILE:", "if False:"),
    ("                if before in paths:\n", "                if True:\n"),
    ("    return pairs, sorted(paths - used)", "    return pairs, sorted(paths)"),
    ("before = path[:-len(tail)] + '/' + old + '.o'", "before = path"),
    ("return name not in changes", "return False"),
    ("    if identical:\n", "    if identical or name in changes:\n"),
]


def mutant(case):
    old, new = case
    source = TOOL.read_text()
    if source.count(old) != 1:
        raise AssertionError('mutation point changed: ' + old)
    with tempfile.TemporaryDirectory(prefix='os32-sep-identity-mut-') as directory:
        path = Path(directory) / 'sep_identity.py'
        path.write_text(source.replace(old, new))
        result = unittest.TextTestRunner(stream=io.StringIO()).run(suite(load(path)))
    if result.errors or not result.failures:
        raise AssertionError('not runtime RED: ' + old + ' -> ' + new)
    return 'runtime RED: ' + old.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if not unittest.TextTestRunner().run(suite(load(TOOL))).wasSuccessful():
        return 1
    if args.mutate:
        for case in MUTANTS:
            print(mutant(case))
        print('sep identity mutants: %d/%d runtime RED' % (len(MUTANTS), len(MUTANTS)))
    return 0


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_sep_identity.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
