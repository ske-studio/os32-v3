#!/usr/bin/env python3
"""PPC_SEP3_DESIGN §2:88 and x86 arch_kapi_read_block define caller capture.
Exercise PPC register/stack words, including the single %s print case.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32

HARNESS = r'''
#include "kapi_call.h"
#include "exec_arch.h"
#include "ppc_frame.h"
#include "kapi_print.h"
static int called, reject, captures, rendered;
static u32 stack_words[10];
int caller_access_get_user(struct caller_access *out) {
    captures++;
    if (reject) return 0;
    out->origin = CALLER_USER; out->generation = 42;
    return 1;
}
int copy_caller_bytes(const struct caller_access *c, const void *src, void *dst, u32 n) {
    if (c->generation != 42 || n != 4 ||
        (uptr)src < (uptr)stack_words ||
        (uptr)src > (uptr)&stack_words[9]) return 0;
    *(u32 *)dst = *(const u32 *)src;
    return 1;
}
int copy_caller_cstr(const struct caller_access *c, const char *src, char *dst, u32 cap) {
    if (c->generation != 42) return 0;
    for (u32 i = 0; i < cap; i++) { dst[i] = src[i]; if (!src[i]) return 1; }
    return 0;
}
static char staging[4096];
void *kmalloc(u32 n) { return n <= sizeof(staging) ? staging : (void *)0; }
void kfree(void *p) { (void)p; }
char kprintf_spec(const char **p, int *w, int *l, char *pad) {
    (void)w; (void)l; (void)pad; return **p;
}
void console_vprintf(u8 attr, const char *fmt, const u32 *args) {
    const char *s = (const char *)(uptr)args[0];
    rendered = attr == 7 && fmt[0] == '%' && fmt[1] == 's' &&
               s[0] == 'o' && s[1] == 'k' && !s[2];
}
static int fake(int a, int b) { called = a == 0x12 && b == 0x34; return -7; }
static void message(const char *s, int n) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static int check(void) {
    struct ppc_frame frame = {0};
    struct arch_syscall call = {&frame};
    struct kapi_arg_source src;
    struct kapi_values values;
    frame.gpr[0] = 0; /* replaced with a two-integer slot by Python */
    frame.gpr[3] = 0x12; frame.gpr[4] = 0x34;
    arch_kapi_source(&src, &call);
    if (arch_syscall_slot(&call) != frame.gpr[0] ||
        !arch_kapi_read_block(&src, 0, 2, &values)) return 2;
    message("reached=invoke\n", 15);
    arch_syscall_result(&call, arch_kapi_invoke((void *)fake, &values, 2));
    if (!called || frame.gpr[3] != (u32)-7) {
        message("FAIL: arguments\n", 16); return 3;
    }
    frame.gpr[3] = 7; frame.gpr[4] = (u32)(uptr)"%s";
    frame.gpr[5] = (u32)(uptr)"ok";
    arch_kapi_source(&src, &call);
    if (!kapi_print_call(&src) || !rendered) return 4;
    if (captures != 2) return 5;
    arch_kapi_source(&src, &call);
    reject = 1;
    if (arch_kapi_read_block(&src, 0, 2, &values) || src.next || src.caller_valid) return 6;
    reject = 0;
    if (!arch_kapi_read_block(&src, 0, 2, &values)) return 7;
    frame.gpr[1] = (u32)(uptr)stack_words;
    stack_words[0] = 0xdead; stack_words[1] = 0xbeef;
    for (unsigned i = 2; i < 8; i++) frame.gpr[3+i] = 100+i;
    for (unsigned i = 0; i < 8; i++) stack_words[2+i] = 108+i;
    for (unsigned i = 2; i < 16; i++) {
        u32 word = 0;
        if (!arch_kapi_va_next(&src, i & 1 ? KAPI_VALUE_POINTER : KAPI_VALUE_INTEGER, &word) ||
            word != 100+i || src.next != i+1) return 8;
    }
    u32 word = 99;
    if (arch_kapi_va_next(&src, KAPI_VALUE_INTEGER, &word) || src.next != 16 || word != 99) return 9;
    src.next = 8; frame.gpr[1] = 0xfffffffcu;
    if (arch_kapi_va_next(&src, KAPI_VALUE_POINTER, &word) || src.next != 8) return 10;
    frame.gpr[1] = 0;
    if (arch_kapi_va_next(&src, KAPI_VALUE_POINTER, &word) || src.next != 8) return 11;
    return 0;
}
void _start(void) {
    int rc = check();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
'''

def execute(source, tmp):
    (tmp / 'call.c').write_text(source)
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
                    '-fno-stack-protector', '-D__cdecl=', '-I' + str(tmp / 'gen/include'),
                    '-I' + str(ROOT / 'arch/ppc'), '-I' + str(ROOT / 'include'),
                    '-I' + str(ROOT / 'sdk/include/os32'), str(tmp / 'call.c'),
                    str(tmp / 'test.c'), str(ROOT / 'sys/kapi/kapi_print.c'), '-I' + str(ROOT / 'lib'), '-I' + str(ROOT / 'mm'), '-o', str(tmp / 'test')], check=True)
    return host32.run([str(tmp / 'test')], capture_output=True, text=True)

def main():
    import json
    entries = json.loads((ROOT / 'sdk/kapi.json').read_text())['api']
    slot = next(i for i, item in enumerate(entries)
                if item['ret'] == 'int' and item.get('args') == ['int x', 'int y'])
    with tempfile.TemporaryDirectory(prefix='ppc-kapi-') as directory:
        tmp = Path(directory)
        subprocess.run([sys.executable, '-B', str(ROOT / 'sdk/gen_kapi.py'),
                        '--target', 'ppc-powermac', '--out-dir', str(tmp / 'gen'),
                        str(ROOT / 'sdk/kapi.json')], check=True, cwd=ROOT)
        (tmp / 'test.c').write_text(HARNESS.replace('frame.gpr[0] = 0;', f'frame.gpr[0] = {slot};'))
        source = (ROOT / 'sys/arch/ppc/kapi_call.c').read_text()
        result = execute(source, tmp)
        if result.returncode != 0:
            print(result.stdout, result.stderr); return 1
        if '--mutate' in sys.argv:
            old = 'frame->gpr[3 + first + i]'
            assert source.count(old) == 1
            result = execute(source.replace(old, 'frame->gpr[4 + first + i]'), tmp)
            if result.returncode != 3 or result.stdout != 'reached=invoke\nFAIL: arguments\n':
                print('mutation unexpected:', result.returncode, result.stdout, result.stderr); return 1
            print('register offset: RED (runtime)')
            for name, old, new, expected in [
                ('caller capture omitted', 'if (!caller_access_get_user(&src->caller)) return 0;', '(void)0;', 4),
                ('stack linkage +8 omitted', '8u + (src->next - 8u)', '(src->next - 8u)', 8),
            ]:
                assert source.count(old) == 1, name
                result = execute(source.replace(old, new), tmp)
                if result.returncode != expected:
                    print(name, result.returncode, result.stdout, result.stderr); return 1
                print(name + ': RED (runtime)')
    return 0

if __name__ == '__main__':
    sys.exit(main())
