#!/usr/bin/env python3
"""T1f scanner: injected violations in a real copied tree, no compilation."""
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
    def test_multiword_pointer_types_and_cast_chains(self):
        for value in (
                '(volatile unsigned char *)MEM_BOOTINFO_BASE',
                '(volatile u8 *)(uptr)MEM_BOOTINFO_BASE',
                '(const volatile struct bootinfo *)(unsigned long)((MEM_BOOTINFO_BASE))',
                '(signed char * const)((uptr)(MEM_BOOTINFO_BASE))',
                '(u8 *)(unsigned int)(uptr)((MEM_BOOTINFO_BASE))'):
            with self.subTest(value=value):
                hits = checker.scan('void probe(void) { volatile u8 *low = ' + value + '; }')
                self.assertIn(('probe', 'physical-cast'), [(h[1], h[2]) for h in hits])

    def test_grouped_physical_sink_operands(self):
        for value in ('(u32)(p)', '(u32)((p))', '(u32)(uptr)(p)',
                      '(unsigned long)((p))', '(u32)(&table[0])'):
            with self.subTest(value=value):
                hits = checker.scan('void probe(void *p) { paging_load_cr3(' + value + '); }')
                self.assertIn(('probe', 'physical-sink'), [(h[1], h[2]) for h in hits])

    def test_converted_physical_sink_expressions(self):
        for value in ('(u32)(4096 + V2P(p))', '(u32)(V2P(p) + 4096)',
                      '(u32)get_phys(p)', '(u32)(4096 + get_phys(p))',
                      '(u32)(get_phys(wrap(p, table)) + V2P(p))'):
            with self.subTest(value=value):
                self.assertEqual(checker.scan(
                    'void probe(void *p) { paging_load_cr3(' + value + '); }'), [])

    def test_pointer_outside_call_arguments(self):
        for value in ('(u32)(V2P(p) + p)', '(u32)(get_phys(p) + p)',
                      '(u32)(p + get_phys(p))'):
            with self.subTest(value=value):
                self.assertIn('physical-sink', [h[2] for h in checker.scan(
                    'void probe(void *p) { paging_load_cr3(' + value + '); }')])

    def test_tree_integer_typedef_casts(self):
        for typename in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32', 'uptr'):
            with self.subTest(typename=typename):
                self.assertIn('physical-cast', [h[2] for h in checker.scan(
                    'void probe(void) { volatile u8 *low = '
                    '(volatile u8 *)(' + typename + ')MEM_BOOTINFO_BASE; }')])

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
    volatile u8 *w = (volatile unsigned char *)(uptr)(P2V_IO(MEM_BOOTINFO_BASE));
    const struct sample *s = (const struct sample *)((P2V(pa)));
    paging_load_cr3((u32)(V2P(p)));
    paging_load_cr3((u32)(pa));
    paging_load_cr3((u32)(paddr));
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

    def test_hostdrv_address_and_integer_member(self):
        self.assertIn('physical-sink', [h[2] for h in checker.scan(
            'void probe(void) { submit((u32)(&g_iostatus)); }')])
        self.assertEqual(checker.scan(
            'void probe(void) { status((unsigned long)g_iostatus.Status); }'), [])

    def test_hostdrv_linear_exceptions_remain_scoped(self):
        # Exercise the real 24 address casts: audited linear ABI exceptions
        # must cover only the 12 named functions, not a new unaudited caller.
        source = (ROOT / 'fs/hostdrvfs.c').read_text()
        entries = [line for line in (ROOT / 'tools/check_p2v_allow.txt').read_text().splitlines()
                   if line.startswith('fs/hostdrvfs.c:')]
        self.assertEqual(len(entries), 12)
        with tempfile.TemporaryDirectory(prefix='os32-hostdrv-p2v-') as tmp:
            root = pathlib.Path(tmp)
            (root / 'fs').mkdir()
            (root / 'tools').mkdir()
            target = root / 'fs/hostdrvfs.c'
            target.write_text(source)
            allow = root / 'tools/check_p2v_allow.txt'
            allow.write_text('\n'.join(entries) + '\n')
            self.assertEqual(checker.audit(root), ([], 12))
            target.write_text(source + '\nvoid unaudited(void) { submit((u32)&g_invoke); }\n')
            errors, _ = checker.audit(root)
            self.assertEqual(len(errors), 1)
            self.assertIn(':unaudited: physical-sink', errors[0])
            target.write_text(source)
            allow.write_text('')
            errors, _ = checker.audit(root)
            self.assertEqual(len(errors), 24)
            self.assertEqual({e.split(':')[2] for e in errors},
                             {e.split(':')[1] for e in entries})

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
        if name.startswith('bootinfo-'):
            target = root / 'kernel/bootinfo.c'
            original = 'volatile u8 *low = (volatile u8 *)P2V_IO(MEM_BOOTINFO_BASE);'
            source = target.read_text()
            if source.count(original) != 1:
                raise AssertionError('bootinfo mutation point changed')
            target.write_text(source.replace(original, body))
            expected = f'bootinfo_capture: {rule}'
        else:
            # p is a declared pointer parameter, including for the CR3 mutant.
            params = 'void *p' if name == 'grouped-cr3-pointer' else 'void'
            (root / 'kernel/p2v_mutant.c').write_text('void p2v_mutant(' + params + ')\n{\n' + body + '\n}\n')
            expected = f'p2v_mutant: {rule}'
        result = subprocess.run([sys.executable, str(ROOT / 'tools/check_p2v.py'), '--root', str(root)],
                                capture_output=True, text=True)
        if result.returncode != 1 or expected not in result.stdout:
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
            ('bootinfo-multiword-type', 'volatile u8 *low = (volatile unsigned char *)MEM_BOOTINFO_BASE;', 'physical-cast'),
            ('bootinfo-intermediate-cast', 'volatile u8 *low = (volatile u8 *)(uptr)MEM_BOOTINFO_BASE;', 'physical-cast'),
            ('bootinfo-signed-cast', 'volatile u8 *low = (volatile u8 *)(i32)MEM_BOOTINFO_BASE;', 'physical-cast'),
            ('grouped-cr3-pointer', '    paging_load_cr3((u32)(p));', 'physical-sink'),
        ]
        for result in run_ordered(mutant, cases):
            print(result)
        print(f'P2V mutants: {len(cases)}/{len(cases)} runtime RED; compile failures: 0')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
