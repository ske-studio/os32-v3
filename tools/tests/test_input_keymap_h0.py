#!/usr/bin/env python3
"""Compile the internal keymap declaration contract without libc."""
CHECK = dict(name='input-keymap-h0', tier=1, budget=5, needs=[], covers=[
    'sys/hal/input/keymap.h', 'sys/include/types.h', 'sys/include/callconv.h',
    'tools/tests/test_input_keymap_h0.py', 'tools/tests/host32.py'],
    basis=['INPUT_KEYMAP_DESIGN §2, §3, K10'], kind='contract')
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = r'''
#include "keymap.h"
#define TYPE(expr, t) _Static_assert(__builtin_types_compatible_p(__typeof__(expr), t), #expr)
#define OFFSET(t, f, n) _Static_assert(__builtin_offsetof(t, f) == n, #f)
TYPE((keysym_t)0, u16);
_Static_assert(sizeof(KmEntry) == 10, "entry size");
OFFSET(KmEntry, command, 0); OFFSET(KmEntry, group1, 2); OFFSET(KmEntry, group2, 6);
TYPE(((KmEntry *)0)->group1[0], keysym_t);
TYPE(((KmEntry *)0)->group2[0], keysym_t);
TYPE(((KmKey *)0)->src, u8); TYPE(((KmKey *)0)->down, u8);
TYPE(((KmKey *)0)->repeat, u8); TYPE(((KmKey *)0)->generation, u16);
TYPE(((KmKey *)0)->keycode, u16);
TYPE(((KmBackend *)0)->id, u16); TYPE(((KmBackend *)0)->count, u16);
TYPE(((KmBackend *)0)->entries, const KmEntry *);
TYPE(((KmBackend *)0)->compat_sc, const u8 *);
TYPE(((KmBackend *)0)->norepeat, const u8 *);
TYPE(((KmBackend *)0)->extensions, const void *);
TYPE(&km_backend_register, int (*)(const KmBackend *));
TYPE(&km_source_bind, int (*)(u8, u16, u16));
TYPE(&input_capture_key, int (*)(const KmKey *));
int check(void) {
    return is_keypad(0xf21f) || !is_keypad(0xf220) ||
           !is_keypad(0xf27f) || is_keypad(0xf280) || is_keypad(0xf295);
}
#ifndef PPC_COMPILE
void _start(void) {
    int result = check();
    __asm__ volatile ("int $0x80" : : "a" (1), "b" (result));
    __builtin_unreachable();
}
#endif
'''

def run(ctx):
    header = (ctx.root / 'sys/hal/input/keymap.h').read_text()
    with tempfile.TemporaryDirectory(prefix='input-keymap-decl-') as td:
        tmp = Path(td)
        (tmp / 'test.c').write_text(SOURCE)
        def build(h):
            (tmp / 'keymap.h').write_text(h)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                            '-nostdlib', '-static', '-no-pie', '-I'+str(tmp),
                            '-I'+str(ctx.root/'include'), str(tmp/'test.c'),
                            '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert build(header) == 0
        print('PASS x86 declaration and keypad boundaries', flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-gcc'
        if cc.is_file() and os.access(cc, os.X_OK):
            subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-DPPC_COMPILE', '-I'+str(tmp),
                            '-I'+str(ctx.root/'include'), '-c', str(tmp/'test.c'),
                            '-o', str(tmp/'test.o')], check=True)
            print('PASS PowerPC declaration cross compile', flush=True)
        else:
            print('SKIP PowerPC cross compile: toolchain unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1': return 1
        if ctx.mutate:
            old = 'sym <= 0xf27f'
            assert header.count(old) == 1
            assert build(header.replace(old, 'sym <= 0xf295')) != 0
            print('RED runtime: keypad upper bound', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
