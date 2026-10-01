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
AST_SCAN = checker.scan
PREAMBLE = '''
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed char i8; typedef short i16; typedef int i32; typedef unsigned int uptr;
#define MEM_BOOTINFO_BASE 0x90000u
#define MEM_GFX_BB_BASE 0x6a000u
#define TVRAM_CHAR_BASE 0xa0000u
#define RING3_HEAP_TOP 0x500000u
#define P2V(x) ((void *)(uptr)(x))
#define P2V_IO(x) ((void *)(uptr)(x))
#define P2V_CONST(x) ((void *)(uptr)(x))
#define P2V_IO_CONST(x) ((void *)(uptr)(x))
#define V2P(x) ((uptr)(x))
struct bootinfo { int x; }; struct sample { void *p; };
struct status_t { u32 Status; }; struct status_t g_iostatus;
u32 pa, paddr, buffer_phys, user_va;
u8 buffer[512], table[4096];
void paging_load_cr3(u32); void dma_chan_setup(int,u32,int,int); void submit(u32); void status(u32);
u32 get_phys(void *); void *wrap(void *, void *);
'''

def scan_valid(source):
    offset = PREAMBLE.count('\n')
    return [(ln-offset,f,r) for ln,f,r in AST_SCAN(PREAMBLE + source)]

checker.scan = scan_valid
sys.path.insert(0, str(ROOT / 'tools/tests'))
from mutpar import run_ordered, mutant_tree


class ScannerTest(unittest.TestCase):
    def test_multiword_pointer_types_and_cast_chains(self):
        for value in (
                '(volatile unsigned char *)MEM_BOOTINFO_BASE',
                '(volatile u8 *)(uptr)MEM_BOOTINFO_BASE',
                '(const volatile struct bootinfo *)(unsigned long)((MEM_BOOTINFO_BASE))',
                '(signed char * const)((uptr)(MEM_BOOTINFO_BASE))',
                '(volatile u8 * const volatile)(uptr const)MEM_BOOTINFO_BASE',
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
    const struct sample *sample = (const struct sample *)((P2V(pa)));
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

    def test_real_tree(self):
        self.assertEqual(checker.audit(ROOT)[0], [])


def mutant(case):
    name, body, rule = case
    with tempfile.TemporaryDirectory(prefix='os32-p2v-') as tmp:
        root = pathlib.Path(tmp)
        root = mutant_tree(ROOT, root / 'tree', {}, real={'kernel/bootinfo.c','kernel/sysclk.c'})
        if name.startswith('bootinfo-'):
            target = root / 'kernel/bootinfo.c'
            original = 'volatile u8 *low = (volatile u8 *)P2V_IO(MEM_BOOTINFO_BASE);'
            source = target.read_text()
            if source.count(original) != 1:
                raise AssertionError('bootinfo mutation point changed')
            target.write_text(source.replace(original, body))
            expected = f'bootinfo_capture: {rule}'
        else:
            target = root / 'kernel/sysclk.c'
            params = 'void *p' if name == 'grouped-cr3-pointer' else 'void'
            target.write_text(target.read_text() + '\n'
                + '#define RING3_HEAP_TOP 0x800000u\nu32 buffer_phys; void paging_load_cr3(u32 pd_phys);\n'
                + 'void p2v_mutant(' + params + ') {\n' + body + '\n}\n')
            expected = f'p2v_mutant: {rule}'
        # A parse error must never satisfy the runtime RED assertion.
        import clang_ast
        for u in clang_ast.units(root):
            if u['src'] == str(target.relative_to(root)):
                clang_ast.parse(u['src'],u['argv'],root)
        result = subprocess.run([sys.executable, str(ROOT / 'tools/check_p2v.py'), '--root', str(root)],
                                capture_output=True, text=True)
        if result.returncode != 1 or expected not in result.stdout or 'clang parse failure' in result.stdout:
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
            ('bootinfo-qualified-pointer', 'volatile u8 *low = (volatile u8 * const volatile)MEM_BOOTINFO_BASE;', 'physical-cast'),
            ('bootinfo-qualified-int', 'volatile u8 *low = (volatile u8 *)(uptr const)MEM_BOOTINFO_BASE;', 'physical-cast'),
        ]
        for result in run_ordered(mutant, cases):
            print(result)
        print(f'P2V mutants: {len(cases)}/{len(cases)} runtime RED; compile failures: 0')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
