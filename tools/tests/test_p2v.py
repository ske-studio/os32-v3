#!/usr/bin/env python3
"""T1f scanner: four injected violations, real copied tree, no compilation."""
import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import check_p2v as checker
sys.path.insert(0, str(ROOT / 'tools/tests'))
from mutpar import run_ordered


class ScannerTest(unittest.TestCase):
    def test_multiline_and_function_context(self):
        hits = checker.scan('void probe(void)\n{\n u8 *p = (u8 *)\n MEM_GFX_BB_BASE;\n}\n')
        self.assertEqual([(h[1], h[2]) for h in hits], [('probe', 'physical-cast')])

    def test_constant_initializers_and_converted_pointer(self):
        source = '''u8 *p = P2V_CONST(MEM_GFX_BB_BASE);
static const struct sample s = {
    P2V_IO_CONST(TVRAM_CHAR_BASE)
};
void probe(void) {
    u32 *p = (u32 *)P2V(pa);
    volatile u8 *v = (volatile u8 *)P2V_IO(pa);
}
'''
        self.assertEqual(checker.scan(source), [])

    def test_inverse_and_nested_user_expression(self):
        self.assertIn('physical-sink', [h[2] for h in checker.scan(
            'void probe(void) { dma_chan_setup(1, (u32)buffer, 512, 0); }')])
        self.assertIn('physical-sink', [h[2] for h in checker.scan(
            'void probe(void *p) { paging_load_cr3((u32)p); }')])
        self.assertIn('user-v2p', [h[2] for h in checker.scan(
            'void probe(void) { V2P((void *)(user_va + 1)); }')])

    def test_comments_and_string_literals(self):
        self.assertEqual(checker.scan('/* (u8 *)pa */\nchar *s = "V2P(RING3_HEAP_TOP)";'), [])

    def test_real_tree(self):
        self.assertEqual(checker.audit(ROOT)[0], [])


def mutant(case):
    name, body, rule = case
    with tempfile.TemporaryDirectory(prefix='os32-p2v-') as tmp:
        root = pathlib.Path(tmp)
        for directory in checker.ROOTS:
            for p in (ROOT / directory).rglob('*'):
                if p.suffix not in ('.c', '.h') or checker.EXCLUDED.intersection(p.parts):
                    continue
                target = root / p.relative_to(ROOT)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(p, target)
        (root / 'tools').mkdir()
        shutil.copyfile(ROOT / 'tools/check_p2v_allow.txt', root / 'tools/check_p2v_allow.txt')
        (root / 'kernel/p2v_mutant.c').write_text('void p2v_mutant(void)\n{\n' + body + '\n}\n')
        result = subprocess.run([sys.executable, str(ROOT / 'tools/check_p2v.py'), '--root', str(root)],
                                capture_output=True, text=True)
        if result.returncode != 1 or f'p2v_mutant: {rule}' not in result.stdout:
            raise AssertionError(f'{name}: expected runtime rejection, rc={result.returncode}\n{result.stdout}{result.stderr}')
        return f'RED: {name} (scanner ran, rc=1)'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ScannerTest)
    if not unittest.TextTestRunner().run(suite).wasSuccessful():
        return 1
    if args.mutate:
        cases = [
            ('constant-cast', '    u8 *p = (u8 *)MEM_GFX_BB_BASE;', 'physical-cast'),
            ('physical-cast-split', '    u8 *p = (u8 *)\n        buffer_phys;', 'physical-cast'),
            ('app-v2p', '    u32 p = V2P((void *)RING3_HEAP_TOP);', 'user-v2p'),
            ('function-const', '    u8 *p = P2V_CONST(MEM_GFX_BB_BASE);', 'function-const'),
        ]
        for result in run_ordered(mutant, cases):
            print(result)
        print('P2V mutants: 4/4 runtime RED; compile failures: 0')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
