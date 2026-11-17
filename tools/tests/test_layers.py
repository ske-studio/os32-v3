#!/usr/bin/env python3
"""LAYER_GUARD_DESIGN §4・§5: 偽の木と偽の .d で層の規則と基準の戻り止めを確かめる。"""
import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'tools/check_layers.py'
OUT = 'build/out'  # 偽の木 (TemporaryDirectory) の中の置き場。実物の成果物は読まない
GEN = OUT + '/ppc-kboot/sh/gen/'


def load(path):
    spec = importlib.util.spec_from_file_location('check_layers_under_test', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def build(root, files, deps):
    """files: {パス: 中身}。deps: {OUT の下の .d: [ソース, ヘッダ...]}。"""
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    for name, words in deps.items():
        path = root / OUT / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('x.o: ' + ' \\\n '.join(words) + '\n\n' + words[-1] + ':\n')


def inc(*names):
    return ''.join('#include ' + (name if name[0] == '<' else '"' + name + '"') + '\n'
                   for name in names)


def suite(tool):
    class LayerTest(unittest.TestCase):
        def setUp(self):
            self.dir = tempfile.TemporaryDirectory(prefix='os32-layers-')
            self.root = Path(self.dir.name)

        def tearDown(self):
            self.dir.cleanup()

        def edges(self, image, files, deps):
            build(self.root, files, deps)
            found, _, _ = tool.measure(self.root, image)
            return {edge: len(units) for edge, units in found.items()}

        def test_common_rules_on_matching_image(self):
            files = {
                'kernel/a.c': inc('arch_io.h', 'idt.h', 'pit.h', 'fdc.h', 'pc98.h', 'ok.h'),
                'arch/x86/arch_io.h': inc('x86_flags.h'), 'arch/x86/x86_flags.h': '',
                'arch/x86/kernel/idt.h': '', 'platform/pc98/pit.h': '',
                'platform/pc98/platform_io.h': '', 'drivers/block/fdc.h': inc('platform_io.h'),
                'include/pc98.h': '', 'include/ok.h': '',
            }
            words = ['kernel/a.c', 'arch/x86/arch_io.h', 'arch/x86/x86_flags.h',
                     'arch/x86/kernel/idt.h', 'platform/pc98/pit.h', 'platform/pc98/platform_io.h',
                     'drivers/block/fdc.h', 'include/pc98.h', 'include/ok.h']
            self.assertEqual(self.edges('x86-pc98', files, {'obj/kernel/a.d': words}), {
                ('R1', 'kernel/a.c', 'arch/x86/kernel/idt.h'): 1,
                ('R2', 'kernel/a.c', 'platform/pc98/pit.h'): 1,
                ('R3', 'kernel/a.c', 'drivers/block/fdc.h'): 1,
                ('R4', 'kernel/a.c', 'include/pc98.h'): 1,
            })

        def test_device_and_arch_readers(self):
            files = {
                'drivers/net/n.c': inc('idt.h', 'pit.h', 'arch_cpu.h'),
                'arch/x86/kernel/v.c': inc('bios.h', 'platform_io.h', 'gdt.h'),
                'platform/pc98/p.c': inc('gdt.h', 'bios.h'),
                'arch/x86/kernel/idt.h': '', 'arch/x86/kernel/gdt.h': '', 'arch/x86/arch_cpu.h': '',
                'platform/pc98/pit.h': '', 'platform/pc98/bios.h': '', 'platform/pc98/platform_io.h': '',
            }
            deps = {
                'lan/n.d': ['drivers/net/n.c', 'arch/x86/kernel/idt.h', 'platform/pc98/pit.h',
                            'arch/x86/arch_cpu.h'],
                'obj/arch/v.d': ['arch/x86/kernel/v.c', 'platform/pc98/bios.h',
                                 'platform/pc98/platform_io.h', 'arch/x86/kernel/gdt.h'],
                'obj/platform/p.d': ['platform/pc98/p.c', 'arch/x86/kernel/gdt.h',
                                     'platform/pc98/bios.h'],
            }
            self.assertEqual(self.edges('x86-pc98', files, deps), {
                ('R5', 'drivers/net/n.c', 'arch/x86/kernel/idt.h'): 1,
                ('R7', 'arch/x86/kernel/v.c', 'platform/pc98/bios.h'): 1,
            })

        def test_foreign_machine_wins(self):
            files = {
                'gfx/g.h': inc('pc98.h'), 'gfx/g.c': inc('g.h', 'legacy_lowmap.h', 'arch_io.h',
                                                         'legacy_guest.h', 'kbd.h'),
                'gfx/h.c': inc('g.h', 'stub.h'), 'drivers/video/fb.c': inc('pc98.h'),
                'arch/x86/kernel/x.c': '', 'platform/none/stub.h': '',
                'include/pc98.h': '', 'include/legacy_lowmap.h': '', 'arch/x86/arch_io.h': '',
                'include/legacy_guest.h': '', 'include/kbd.h': '', 'platform/none/null_provider.c': '',
                'gfx/only_shell.c': inc('pc98.h'), 'gfx/only_neg.c': inc('pc98.h'),
                'gfx/only_fd1.c': inc('pc98.h'), OUT + '/ppc-kboot/gen/exports.c': inc('pc98.h'),
                'gfx/old.h': inc('tvram.h'), 'include/tvram.h': '',
            }
            deps = {
                'ppc-kboot/gfx/g.d': ['gfx/g.c', 'gfx/g.h', 'include/pc98.h', 'include/legacy_lowmap.h',
                                      'arch/x86/arch_io.h', 'include/legacy_guest.h', 'include/kbd.h'],
                'ppc-kboot/platform/none/l.d': ['platform/none/null_provider.c'],
                'ppc-kboot/shell/gfx/s.d': ['gfx/only_shell.c', 'include/pc98.h'],
                'ppc-kboot/neg1/gfx/n.d': ['gfx/only_neg.c', 'include/pc98.h'],
                'ppc-kboot/link/fd1-stream/gfx/f.d': ['gfx/only_fd1.c', 'include/pc98.h'],
                'ppc-kboot/gen/exports.d': [OUT + '/ppc-kboot/gen/exports.c', 'include/pc98.h'],
                'ppc-kboot/gfx/gone.d': ['gfx/gone.c', 'gfx/old.h', 'include/tvram.h'],
                'ppc-kboot/sh/gfx/h.d': ['gfx/h.c', 'gfx/g.h', 'include/pc98.h',
                                         'platform/none/stub.h'],
                'ppc-kboot/drivers/fb.d': ['drivers/video/fb.c', 'include/pc98.h'],
                'ppc-kboot/arch/x.d': ['arch/x86/kernel/x.c'],
            }
            self.assertEqual(self.edges('ppc-powermac', files, deps), {
                ('R6', 'gfx/g.h', 'include/pc98.h'): 2,
                ('R6', 'gfx/g.c', 'include/legacy_lowmap.h'): 1,
                ('R6', 'gfx/g.c', 'arch/x86/arch_io.h'): 1,
                ('R6', 'drivers/video/fb.c', 'include/pc98.h'): 1,
                ('R6', 'arch/x86/kernel/x.c', '-'): 1,
                ('R2', 'gfx/h.c', 'platform/none/stub.h'): 1,
            })

        def test_include_resolution(self):
            files = {
                'net/a.c': inc('clock.h', '../gfx/v2/deep.h', 'absent.h') + '/*\n' + inc('idt.h') + '*/\n'
                           '// ' + inc('idt.h'),
                'net/b.c': inc('<render.h>', '<stdint.h>'), 'net/render.h': '',
                'platform/pc98/render.h': '',
                'net/clock.h': '', 'platform/pc98/clock.h': '', 'gfx/v2/deep.h': inc('clock.h'),
                'arch/x86/kernel/idt.h': '', 'platform/pc98/absent.h': '',
                'platform/pc98/sysclock.h': '',
            }
            words = ['net/a.c', 'net/clock.h', 'platform/pc98/clock.h', 'gfx/v2/deep.h',
                     'arch/x86/kernel/idt.h', 'platform/pc98/sysclock.h']
            deps = {'obj/net/a.d': words,
                    'sqlite-proto/b.d': ['net/b.c', 'net/render.h', 'platform/pc98/render.h',
                                         '/usr/include/stdint.h']}
            self.assertEqual(self.edges('x86-pc98', files, deps), {
                ('R2', 'gfx/v2/deep.h', 'platform/pc98/clock.h'): 1,
                ('R2', 'net/b.c', 'platform/pc98/render.h'): 1,
            })

        def test_generated_kapi_and_build_headers(self):
            files = {
                GEN + 'kapi/kapi_generated.c': inc('tvram.h', 'gen.h'),
                GEN + 'gen.h': inc('pc98.h'),
                'include/tvram.h': '', 'include/pc98.h': '',
            }
            words = [GEN + 'kapi/kapi_generated.c', 'include/tvram.h',
                     GEN + 'gen.h', 'include/pc98.h', '/usr/include/x.h']
            build(self.root, files, {'ppc-kboot/sh/gen/kapi/kapi_generated.d': words})
            found, units, empty = tool.measure(self.root, 'ppc-powermac')
            self.assertEqual(set(found), {('R6', 'kapi/kapi_generated.c', 'include/tvram.h')})
            self.assertEqual((units, empty), ({'kapi/kapi_generated.c'}, []))

        def run_tool(self, *argv):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = tool.main(['--root', str(self.root), *argv])
            return rc, out.getvalue()

        def test_baseline_ratchet(self):
            build(self.root, {'kernel/a.c': inc('pit.h'), 'platform/pc98/pit.h': ''},
                  {name + '/a.d': ['kernel/a.c', 'platform/pc98/pit.h']
                   for name in tool.IMAGES['x86-pc98'][2]})
            path = self.root / 'tools/layers_baseline.json'
            path.parent.mkdir()
            kept = [['R6', 'gfx/g.h', 'include/pc98.h']]
            path.write_text(json.dumps({'ppc-powermac': kept}))
            rc, text = self.run_tool('--image', 'x86-pc98')
            self.assertEqual(rc, 1)
            self.assertIn('new violation: R2 kernel/a.c platform/pc98/pit.h', text)
            self.assertEqual(self.run_tool('--image', 'x86-pc98', '--write-baseline')[0], 0)
            data = json.loads(path.read_text())
            self.assertEqual(data, {'ppc-powermac': kept,
                                    'x86-pc98': [['R2', 'kernel/a.c', 'platform/pc98/pit.h']]})
            rc, text = self.run_tool('--image', 'x86-pc98')
            self.assertEqual(rc, 0)
            self.assertIn('x86-pc98: units=1 edges=1 R2=1', text)
            data['x86-pc98'].append(['R1', 'kernel/a.c', 'arch/x86/kernel/idt.h'])
            path.write_text(json.dumps(data))
            rc, text = self.run_tool('--image', 'x86-pc98')
            self.assertEqual(rc, 1)
            self.assertIn('stale baseline (remove): R1 kernel/a.c arch/x86/kernel/idt.h', text)

        def test_unbuilt_image_fails(self):
            build(self.root, {'kernel/a.c': ''}, {'obj/a.d': ['kernel/a.c']})
            (self.root / 'tools').mkdir()
            (self.root / 'tools/layers_baseline.json').write_text('{"x86-pc98": []}')
            rc, text = self.run_tool('--image', 'x86-pc98')
            self.assertEqual(rc, 1)
            self.assertIn('no .d under ' + OUT + '/cd', text)

    return unittest.defaultTestLoader.loadTestsFromTestCase(LayerTest)


MUTANTS = [
    ("        return 'R6'", "        return None"),
    ("return 'R1' if dst[0] == 'arch' else 'R2'", "return 'R1'"),
    ("return 'R1' if dst[0] == 'arch' else 'R2'", "return 'R2'"),
    ("return 'R3'", "return None"),
    ("return 'R4'", "return None"),
    ("return 'R5'", "return None"),
    ("return 'R7'", "return None"),
    ("and parts[2].startswith(parts[0] + '_')", "and False"),
    ("not in (platform, 'none')", "!= platform"),
    ("if mark == '\"' and local in files:", "if False:"),
    ("path == tail or path.endswith('/' + tail)", "path.endswith(os.path.basename(tail))"),
    ("INCLUDE.findall(COMMENT.sub(' ', text))", "INCLUDE.findall(text)"),
    ("unit = 'kapi/kapi_generated.c'", "continue"),
    ("edges.setdefault(('R6', unit, '-'), set()).add(unit)", "pass"),
    ("reader = unit if path == real else path", "reader = unit"),
    ("if path.startswith('build/') and path != real:", "if False:"),
    ("sorted(observed - expected)", "[]"),
    ("sorted(expected - observed)", "[]"),
    ("empty.append(dep_root)", "pass"),
    ("if not dep.relative_to(base).parts[0].startswith(skip)", "if True"),
    ("and SKIP_ANY not in dep.relative_to(base).parts]", "]"),
    ("elif not inside(real) or real.startswith('build/'):", "elif not inside(real):"),
    ("if mark == '\"' and local in files:", "if local in files:"),
    ("'kdebug', 'lan', 'pcm', 'sqlite-proto', 'trident', 'v86'", "'kdebug', 'lan', 'pcm', 'trident', 'v86'"),
    ("if not words or not (root / words[0]).is_file():", "if not words:"),
    ("baseline[image] = [list(edge) for edge in sorted(edges)]",
     "baseline = {image: [list(edge) for edge in sorted(edges)]}"),
]


def mutant(case):
    old, new = case
    source = TOOL.read_text()
    if source.count(old) != 1:
        raise AssertionError('mutation point changed: ' + old)
    with tempfile.TemporaryDirectory(prefix='os32-layers-mut-') as tmp:
        path = Path(tmp) / 'check_layers.py'
        path.write_text(source.replace(old, new))
        result = unittest.TextTestRunner(stream=io.StringIO()).run(suite(load(path)))
    if result.errors or not result.failures:
        raise AssertionError('not runtime RED: ' + old + ' -> ' + new)
    return 'runtime RED: ' + old


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if not unittest.TextTestRunner().run(suite(load(TOOL))).wasSuccessful():
        return 1
    if args.mutate:
        for case in MUTANTS:
            print(mutant(case))
        print('layers mutants: %d/%d runtime RED' % (len(MUTANTS), len(MUTANTS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
