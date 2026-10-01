#!/usr/bin/env python3
"""Clang regressions: every policy mutant is valid C, and must fail at runtime."""
import argparse
import os
from unittest import mock
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT / 'tools'))
import clang_ast as a
from clang_ast.dialect import findings as dialect
import check_arch_asm as arch
import check_p2v as p2v
from clang_ast.asm import templates
from mutpar import mutant_tree, run_ordered


class ClangTest(unittest.TestCase):
    def tu(self, body, flags=()):
        return a.parse('kernel/clang_probe.c', ['-std=gnu11','-ffreestanding']+list(flags),
                       ROOT,text=body)

    def test_cross_free_newlib_headers(self):
        with tempfile.TemporaryDirectory() as tmp:
            inc = pathlib.Path(tmp)
            (inc/'stdio.h').write_text('#define OS32_NEWLIB_SENTINEL 123\n')
            with mock.patch.object(a,'SYSTEM_NEWLIB',inc), \
                    mock.patch.object(a.shutil,'which',return_value=None), \
                    mock.patch.dict(os.environ,{'CROSS_DIR':str(inc/'missing-cross')}):
                self.tu('#include <stdio.h>\n'
                        '_Static_assert(OS32_NEWLIB_SENTINEL == 123,"newlib");')

    def test_dialect_past_limits(self):
        for flags in ([],['-P'],['-dM'],['-C'],['-CC']):
            tu = self.tu('#line 12 "/outside/hidden.c"\nint *restrict p;\n',flags)
            self.assertTrue(any(w == 'restrict' for _,_,w in dialect(tu,ROOT,{'restrict'},set())))
        for body in ('_Pragma("region restrict")\nint restrict$x;',
                     'const char *s = "restrict"; /* restrict */',
                     '#if 0\nint *restrict p;\n#endif\nint x;'):
            self.assertEqual(dialect(self.tu(body),ROOT,{'restrict'},set()),[])
        self.assertEqual(a.build.strip_jobserver('-j 4 -- X=1'),'-- X=1')

    def test_anonymous_and_old_style(self):
        for body,rule in [('struct outer { union { int x; unsigned y; }; };','anonymous record'),
                          ('int f(x) int x; { return x; }','old-style definition')]:
            self.assertIn(rule,[w for _,_,w in dialect(self.tu(body),ROOT,{rule},set())])
        for body in ('struct { int x; } named;',
                     'struct outer { struct { int x; } *p; };',
                     'struct outer { struct { int x; } a[2]; };'):
            self.assertEqual(dialect(self.tu(body),ROOT,{'anonymous record'},set()),[])

    def test_typedef_atomic_tls_restrict(self):
        for body,rule in [('typedef _Atomic int A; A x;','_Atomic'),
                          ('__thread int x;','__thread'),
                          ('typedef int *restrict R; R x;','restrict'),
                          ('_Atomic(int) f(void);','_Atomic'),
                          ('int *restrict f(void);','restrict'),
                          ('typedef _Atomic(int) (*Fn)(void); Fn fp;','_Atomic')]:
            self.assertIn(rule,[w for _,_,w in dialect(self.tu(body),ROOT,{rule},set())])

    def test_align_lexical_variants(self):
        for value in ('(unsigned int *)(b+1)','(U *)(b+1)','(U * const volatile)(b+1)'):
            body = 'typedef unsigned int U; U f(unsigned char *b) { return *'+value+'; }'
            tu = self.tu(body)  # parse success required before runtime rule assertion
            self.assertTrue(any(a.alignment_cast(c) for c,_ in a.walk(tu.cursor)))
        tu = self.tu('unsigned char *f(unsigned int *p) { return (unsigned char *)p; }')
        self.assertFalse(any(a.alignment_cast(c) for c,_ in a.walk(tu.cursor)))

    def test_asm_templates(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root/'kernel').mkdir()
            for body in ('void f(void) { __asm__ volatile("cl" "i\\n\\t" "hlt"); }',
                         '#define STOP "hlt"\nvoid f(void) { __asm__(STOP); }',
                         '#define STOP() __asm__("hlt")\nvoid f(void) { STOP(); }',
                         '#define STOP(x) __asm__(x)\nvoid f(void) { STOP("cli"); }',
                         '#define STOP(x) __asm__("cl" #x)\nvoid f(void) { STOP(i); }',
                         '#define CAT(a,b) a ## b\n#define STR(x) #x\n#define EXP(x) STR(x)\n'
                         'void f(void) { __asm__(EXP(CAT(cl,i))); }'):
                src = root/'kernel/test.c';src.write_text(body)
                tu = a.parse('kernel/test.c',['-std=gnu11'],root)
                self.assertTrue(arch.findings(tu,root))
                self.assertEqual(templates(tu),templates(tu,force_printed=True))
            body = '/* ARCH-ASM-OK: indivisible sequence */\nvoid f(void) { __asm__("cli; hlt"); }'
            (root/'kernel/test.c').write_text(body)
            self.assertEqual(arch.findings(a.parse('kernel/test.c',['-std=gnu11'],root),root),[])
            body = 'void f(void) { const char *s="ARCH-ASM-OK"; (void)s; __asm__("cli"); }'
            (root/'kernel/test.c').write_text(body)
            self.assertTrue(arch.findings(a.parse('kernel/test.c',['-std=gnu11'],root),root))
            body = '/* asm("cli") */\nvoid f(void) { const char *s="hlt"; (void)s; __asm__("nop"); }'
            (root/'kernel/test.c').write_text(body)
            self.assertEqual(arch.findings(a.parse('kernel/test.c',['-std=gnu11'],root),root),[])

    def test_nonconstant_static_assert(self):
        body = 'struct S { int x,y; }; _Static_assert((unsigned long)&((struct S *)0)->y == 4,"x");'
        self.assertIn('nonconstant static assert',[w for _,_,w in dialect(
            self.tu(body),ROOT,{'nonconstant static assert'},set())])

    def test_physical_function_result(self):
        body = ('unsigned long get_phys(void *);\n'
                'void f(void *p) { char *q = (char *)get_phys(p); (void)q; }')
        self.assertIn('physical-cast',[r for _,_,r in p2v.scan(body)])

    def test_linker_layout_value(self):
        body = ('typedef unsigned long u32; extern u32 __bss_end;\n'
                '#define MEM_SHM_BASE (((u32)&__bss_end)+4096)\n'
                'void paging_map_range(u32 vstart,u32 vend,u32 phys,u32 flags);\n'
                'void f(void) { paging_map_range(0,0,MEM_SHM_BASE,0); }')
        self.assertEqual(p2v.scan(body),[])

    def test_inline_v2p(self):
        body = ('typedef unsigned long u32; static inline u32 V2P(const void *p) { return (u32)p; }\n'
                '#define RING3_HEAP_TOP 0x800000u\n'
                'void f(void) { u32 x = V2P((void *)RING3_HEAP_TOP); (void)x; }')
        self.assertIn('user-v2p',[r for _,_,r in p2v.scan(body)])

    def test_parse_failure_is_error(self):
        with self.assertRaises(a.ParseError):
            self.tu('this is not C;')
        with self.assertRaises(a.ParseError):
            self.tu('int x;', ['-std=not-a-dialect'])
        # Clang 21 accepts GNU raw strings; older frontends may reject them.
        try:
            raw = self.tu('const char *s = R"(restrict)";')
        except a.ParseError:
            pass  # Explicit parse failure, never counted as a policy mutant RED.
        else:
            self.assertEqual(dialect(raw,ROOT,{'restrict'},set()),[])

    def test_p2v_alias_and_qualifiers(self):
        for cast in ('(Ptr)(Phys const)MEM_TEST_BASE',
                     '(unsigned char * const volatile)(Phys const)MEM_TEST_BASE'):
            body = ('typedef unsigned long Phys; typedef unsigned char *Ptr;\n'
                    '#define MEM_TEST_BASE 0x100000u\nvoid f(void) { Ptr p = '+cast+'; (void)p; }')
            self.assertIn('physical-cast',[r for _,_,r in p2v.scan(body)])


def mutant(case):
    path,old,new = case
    original = (ROOT/path).read_text()
    if original.count(old) != 1:
        raise AssertionError('mutation point changed: '+path)
    with tempfile.TemporaryDirectory(prefix='clang-rule-mut-') as tmp:
        tree = mutant_tree(ROOT,pathlib.Path(tmp)/'tree',{path:original.replace(old,new)},
                          real={path,'tools/tests/test_clang_ast.py'})
        # Python compilation is separate, never counted as RED.
        compile((tree/path).read_text(),str(tree/path),'exec')
        r = subprocess.run([sys.executable,str(tree/'tools/tests/test_clang_ast.py')],
                           cwd=tree,capture_output=True,text=True,stdin=subprocess.DEVNULL)
        if r.returncode != 1 or 'AssertionError' not in r.stderr or 'ERROR:' in r.stderr:
            raise AssertionError(f'not runtime RED: {path}\n{r.stdout}{r.stderr}')
        return 'runtime RED: '+path


def main():
    p = argparse.ArgumentParser();p.add_argument('--mutate',action='store_true');args=p.parse_args()
    if not unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(ClangTest)).wasSuccessful():
        return 1
    if args.mutate:
        cases = [('tools/clang_ast/__init__.py','return a > 0 and b > a','return False'),
                 ('tools/check_arch_asm.py',"hits.add((rel,c.location.line,match[1]))",'pass'),
                 ('tools/clang_ast/dialect.py',"result.add('_Atomic')",'pass'),
                 ('tools/check_p2v.py',"add('physical-cast')",'pass')]
        for result in run_ordered(mutant,cases):
            print(result)
        print(f'Clang rules: {len(cases)}/{len(cases)} runtime RED; compile failures: 0')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
