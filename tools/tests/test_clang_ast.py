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
import check_le_access as le
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
                    mock.patch.dict(os.environ,{'CROSS_DIR':str(inc/'missing-cross'), 'OS32_CLANG_CROSS_FREE':'1'}):
                self.tu('#include <stdio.h>\n'
                        '_Static_assert(OS32_NEWLIB_SENTINEL == 123,"newlib");')

    def test_review_physical_api_position(self):
        # Use the real header, including the pstart spelling from the review.
        tu = self.tu('#include "kernel/paging.h"\n'
            'void probe(struct addrspace *as, void *p) { '
            'paging_addrspace_map_user_range_phys(as, 0, 0, (u32)p, 0); }',
            ['-I.','-Iinclude'])
        self.assertIn('physical-sink',[r for _,_,_,r in p2v.findings(tu)[0]])
        body = ('typedef unsigned int u32; struct addrspace; '
            'int paging_addrspace_map_user_range_phys(struct addrspace *,u32,u32,u32,u32);'
            'void f(struct addrspace *as,void *p) {'
            'paging_addrspace_map_user_range_phys(as,0,0,(u32)p,0); }')
        self.assertIn('physical-sink',[r for _,_,r in p2v.scan(body)])

    def test_review_conversion_scope(self):
        prefix = ('typedef unsigned int u32; u32 V2P(void *); '
                  'void *P2V(u32); void *P2V_IO(u32); ')
        for expr in ('phys + V2P(p)', 'phys + (u32)P2V(phys)',
                     'phys + (u32)P2V_IO(phys)'):
            self.assertIn('physical-cast',[r for _,_,r in p2v.scan(prefix +
                'void f(u32 phys,void *p) { char *q = (char *)('+expr+'); (void)q; }')])
        self.assertEqual(p2v.scan(prefix +
            'void f(u32 phys) { char *q = (char *)((P2V(phys))); (void)q; }'),[])
        macro = ('#define P2V_CONST(x) ((void *)(unsigned int)(x))\n'
                 '#define GOOD ((char *)P2V_CONST(0x100000))\n'
                 '#define BAD ((char *)(0x100000 + (unsigned int)P2V_CONST(0x100000)))\n')
        self.assertEqual(p2v.scan(macro + 'char *q = GOOD;'),[])
        self.assertIn('physical-cast',[r for _,_,r in p2v.scan(macro + 'char *q = BAD;')])

    def test_review_gcc_branches(self):
        # CI's explicit limited mode cannot make this equivalence claim.
        if os.environ.get('OS32_CLANG_CROSS_FREE') == '1':
            self.skipTest('limited cross-free CI: GCC branch equivalence unverified')
        tu = self.tu('#if __GNUC__ >= 5\nint *restrict p;\n#endif\n'
                     '#if defined(__clang__) || defined(__llvm__) || defined(__has_feature) || '
                     'defined(__has_embed) || defined(__has_constexpr_builtin)\n'
                     '#error clang branch must be disabled\n#endif')
        self.assertIn('restrict',[w for _,_,w in dialect(tu,ROOT,{'restrict'},set())])
        self.tu('#include <stddef.h>\n#include <stdatomic.h>\n'
                '_Static_assert(sizeof(size_t) == 4, "i386");')
        # Missing compiler fails closed unless the caller explicitly opts in.
        with mock.patch.object(a.shutil,'which',return_value=None), \
                mock.patch.dict(os.environ,{'CROSS_DIR':'/missing-cross',
                                           'OS32_CLANG_CROSS_FREE':'0'}):
            with self.assertRaises(a.ParseError):
                self.tu('int x;')

    def test_review_aligned_le_access(self):
        tu = self.tu('typedef unsigned int u32; '
                     'u32 f(u32 *disk) { return *(u32 *)&disk[1]; }')
        casts = [c for c,_ in a.walk(tu.cursor) if c.kind == a.K.CSTYLE_CAST_EXPR]
        self.assertFalse(any(a.alignment_cast(c) for c in casts))
        self.assertTrue(any(le.direct_access(c) for c in casts))
        for body in ('typedef unsigned int U; U f(U *disk) { U *p=(U *)&disk[1]; return *p; }',
                     '#define RD(p) (*(unsigned int *)(p))\n'
                     'unsigned int f(unsigned int *disk) { return RD(&disk[1]); }'):
            self.assertTrue(any(le.direct_access(c) for c,_ in a.walk(self.tu(body).cursor)))

    def test_review_expression_types(self):
        for body,rule in [
            ('int *f(void *p) { return (int *restrict)p; }','restrict'),
            ('#define R restrict\nint *f(void *p) { return (int *R)p; }','restrict'),
            ('#define JOIN(a,b) a ## b\n'
             'int *f(void *p) { return (int *JOIN(re,strict))p; }','restrict'),
            ('int f(void) { return sizeof(int *restrict); }','restrict'),
            ('int f(int n) { return sizeof(int[n]); }','VLA'),
            ('int f(int n) { return sizeof(int (*)[n]); }','VLA'),
            ('int f(int n) { return _Alignof(int[n]); }','VLA'),
            ('#define ARR(n) int[n]\nint f(int n) { return sizeof(ARR(n)); }','VLA'),
            ('#define A _Atomic(int)\nint f(void) { return sizeof(A); }','_Atomic'),
            ('int *f(void) { return (int *restrict){0}; }','restrict')]:
            self.assertIn(rule,[w for _,_,w in dialect(self.tu(body),ROOT,{rule},set())])
        self.assertEqual(dialect(self.tu('int f(void) { return sizeof(int *); }'),
                                 ROOT,{'restrict','_Atomic'},set()),[])

    def test_array_parameter_restrict(self):
        contexts = [
            'int f(int a[{qual}]);',
            'int f(int a[{qual}]) {{ return a[0]; }}',
            'typedef int F(int a[{qual}]);',
            'enum {{ E = sizeof(int (*)(int a[{qual}])) }};',
        ]
        for context in contexts:
            for qual in ('restrict 3', 'restrict', 'static restrict 3',
                         'const volatile restrict 3'):
                with self.subTest(context=context, qualifier=qual):
                    hits = dialect(self.tu(context.format(qual=qual)),
                                   ROOT, {'restrict'}, set())
                    self.assertIn('restrict', [rule for _, _, rule in hits])
            for qual in ('3', 'static 3', 'const', 'volatile 3',
                         'static const volatile 3'):
                with self.subTest(context=context, qualifier=qual):
                    self.assertEqual(dialect(self.tu(context.format(qual=qual)),
                                             ROOT, {'restrict'}, set()), [])
        self.assertTrue(dialect(self.tu(
            '#define R restrict\nint f(int a[R 3]);'), ROOT, {'restrict'}, set()))

    def test_review_all_type_occurrences(self):
        # Each TU must parse successfully before policy assertions can be RED.
        contexts = [
            '_Static_assert(sizeof(TYPE) == 4, "ok");',
            'enum E { X = sizeof(TYPE) };',
            'struct S { unsigned x : sizeof(TYPE); };',
            'typedef int A[sizeof(TYPE)];',
            'struct S { int a[sizeof(TYPE)]; };',
            'void f(int a[sizeof(TYPE)]);',
            'typedef int (*Fn)(int a[sizeof(TYPE)]);',
            'typedef __typeof__(sizeof(TYPE)) Size;',
            'enum E { X = _Alignof(TYPE) };',
            'int f(void) { return _Generic(0, TYPE: 1, default: 0); }',
            'void f(void (*arg)(TYPE));',
            'TYPE f(void);',
            'struct S { TYPE x; };',
            'typedef TYPE A;',
        ]
        for context in contexts:
            for typ, rule in [('int *restrict', 'restrict'), ('_Atomic(int)', '_Atomic')]:
                with self.subTest(context=context, typ=typ):
                    body = context.replace('TYPE', typ)
                    self.assertIn(rule, [w for _,_,w in dialect(
                        self.tu(body), ROOT, {rule}, set())])
            with self.subTest(context=context, typ='int *'):
                self.assertEqual(dialect(self.tu(context.replace('TYPE', 'int *')),
                    ROOT, {'restrict', '_Atomic'}, set()), [])
        for body in [
            '#define R restrict\n_Static_assert(sizeof(int *R) == 4, "ok");',
            '#define JOIN(a,b) a ## b\nenum E { X = sizeof(int *JOIN(re,strict)) };',
            '#define A _Atomic(int)\n_Static_assert(sizeof(A) == 4, "ok");',
        ]:
            self.assertTrue(dialect(self.tu(body), ROOT, {'restrict', '_Atomic'}, set()))
        self.assertEqual(dialect(self.tu(
            '_Static_assert(sizeof(int *) == 4, "restrict _Atomic");'
            'enum E { X = sizeof(int *) /* restrict _Atomic */ };'),
            ROOT, {'restrict', '_Atomic'}, set()), [])

    def test_type_occurrences_header_root(self):
        # SourceManager may return relative include paths; resolve against the
        # requested root, and normalize .. before policy/vendor filtering.
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / 'kernel').mkdir()
            (root / 'headers').mkdir()
            for typ, rule in [('int *restrict', 'restrict'), ('_Atomic(int)', '_Atomic')]:
                (root / 'headers/probe.h').write_text(
                    '_Static_assert(sizeof(' + typ + ') == 4, "ok");\n')
                tu = a.parse('kernel/probe.c', ['-std=gnu11', '-I.'], root,
                    text='#include <headers/probe.h>\n')
                self.assertIn(('headers/probe.h', 1, rule),
                    dialect(tu, root, {rule}, set()))
                tu = a.parse('kernel/probe.c', ['-std=gnu11'], root,
                    text='#include "../headers/probe.h"\n')
                self.assertIn(('headers/probe.h', 1, rule),
                    dialect(tu, root, {rule}, set()))

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
        if path.endswith('.py'):
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
        cases = [('tools/clang_ast/type_occurrences.cpp',
                  'if (array->getIndexTypeQualifiers().hasRestrict())', 'if (false)'),
                 ('tools/clang_ast/__init__.py','return a > 0 and b > a','return False'),
                 ('tools/check_arch_asm.py',"hits.add((rel,c.location.line,match[1]))",'pass'),
                 ('tools/clang_ast/type_occurrences.cpp', 'if (type->isAtomicType())', 'if (false)'),
                 ('tools/check_p2v.py',"add('physical-cast')",'pass'),
                 ('tools/check_p2v.py',"'paging_addrspace_map_user_range_phys': {3}",
                  "'paging_addrspace_map_user_range_phys': set()"),
                 ('tools/check_p2v.py',"if ast.pointer(c.type) and ast.integer(operand.type) and not converted(c):",
                  "if ast.pointer(c.type) and ast.integer(operand.type) and not converted(c) and not any(x.spelling in CONVERSIONS for x in expression_nodes(operand)):"),
                 ('tools/clang_ast/__init__.py',"out += gcc_predefines(cross, tuple(argv), str(root))",'pass'),
                 ('tools/check_le_access.py',"ast.integer(pointee) and pointee.get_size() > 1",'False'),
                 ('tools/clang_ast/dialect.py',"hits = {hit for hit in type_occurrences(tu, root) if hit[2] in words}",
                  'hits = set()'),
                 ('tools/clang_ast/type_occurrences.cpp', 'visitor.TraverseDecl(context.getTranslationUnitDecl());',
                  '(void)visitor;'),
                 ('tools/clang_ast/type_occurrences.py', 'os.path.abspath(root / file)',
                  'os.path.abspath(file)')]
        for result in run_ordered(mutant,cases):
            print(result)
        print(f'Clang rules: {len(cases)}/{len(cases)} runtime RED; compile failures: 0')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
