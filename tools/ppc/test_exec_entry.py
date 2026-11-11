#!/usr/bin/env python3
"""Exercise the PPC entry preparation with a fake USER stack."""
CHECK = dict(name='ppc-exec-entry', tier=1,
             covers=['arch/ppc/exec_entry.c', 'arch/ppc/arch_exec_storage.h',
                     'include/exec_arch.h', 'include/types.h',
                     'tools/ppc/test_exec_entry.py'],
             inputs=['arch/ppc/exec_entry.c', 'arch/ppc/arch_exec_storage.h',
                     'include/exec_arch.h', 'include/types.h'], needs=[], budget=3)
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/tests"))
import host32
SOURCE = ROOT / 'arch/ppc/exec_entry.c'
HARNESS = r'''
#include "exec_arch.h"
struct view { u32 pc, sp; struct exec_entry_args args; };
void *memcpy(void *dst, const void *src, unsigned n) {
    unsigned char *d = dst; const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
int memcmp(const void *a, const void *b, unsigned n) {
    const unsigned char *x = a, *y = b;
    while (n--) { if (*x != *y) return 1; x++; y++; }
    return 0;
}
static int fail_store;
static u32 address, words[4];
static int store(void *owner, u32 addr, const void *src, u32 len) {
    (void)owner; address = addr;
    if (fail_store) return -1;
    if (len != sizeof(words)) return -1;
    __builtin_memcpy(words, src, len); return 0;
}
static void message(const char *s, int n) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static int check(void) {
    struct arch_entry_context out, before;
    struct exec_entry_args args = {3, 0x1234, 0x5678, 0x9abc};
    struct view v;
    for (unsigned i = 0; i < sizeof(out.opaque); i++) out.opaque[i] = 0xa5;
    before = out;
    if (arch_entry_prepare(&out, 15, 42, 1, &args, store, 0) == 0 ||
        __builtin_memcmp(&out, &before, sizeof(out))) return 2;
    fail_store = 1;
    if (arch_entry_prepare(&out, 0x100f, 42, 1, &args, store, 0) == 0 ||
        __builtin_memcmp(&out, &before, sizeof(out))) return 3;
    fail_store = 0;
    if (arch_entry_prepare(&out, 0x100f, 42, 1, &args, store, 0)) return 4;
    __builtin_memcpy(&v, out.opaque, sizeof(v));
    message("reached=entry\n", 14);
    if (v.sp != 0xff0 || address != v.sp) { message("FAIL: alignment\n", 16); return 5; }
    if (v.pc != 42 || __builtin_memcmp(&v.args, &args, sizeof(args)) ||
        words[0] || words[1] || words[2] || words[3]) return 6;
    return 0;
}
void _start(void) {
    int rc = check();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
'''

def execute(source):
    with tempfile.TemporaryDirectory(prefix='ppc-entry-') as d:
        tmp = Path(d)
        (tmp / 'entry.c').write_text(source)
        (tmp / 'test.c').write_text(HARNESS)
        cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
               '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-fno-stack-protector',
               '-I' + str(ROOT / 'arch/ppc'), '-I' + str(ROOT / 'include'),
               str(tmp / 'entry.c'), str(tmp / 'test.c'), '-o', str(tmp / 'test')]
        subprocess.run(cmd, check=True)
        return host32.run([str(tmp / 'test')], capture_output=True, text=True)

def main():
    original = SOURCE.read_text()
    result = execute(original)
    print(result.stdout, end='')
    if result.returncode != 0:
        return 1
    if '--mutate' in sys.argv:
        old = '(top & ~(u32)15) - 16'
        assert original.count(old) == 1
        result = execute(original.replace(old, 'top - 16'))
        if result.returncode != 5 or result.stdout != 'reached=entry\nFAIL: alignment\n':
            print('mutation unexpected:', result.returncode, result.stdout, result.stderr)
            return 1
        print('alignment: RED (1)')
    return 0

if __name__ == '__main__':
    sys.exit(main())
