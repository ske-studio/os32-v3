#!/usr/bin/env python3
"""Compile the internal keymap declaration contract without libc."""
CHECK = dict(name='input-keymap-h0b', tier=1, budget=5, needs=[], covers=[
    'input/hal/keymap.h', 'include/types.h', 'include/callconv.h',
    'tools/tests/test_input_keymap_h0b.py', 'input/hal/in2_dev.c', 'input/hal/in2_int.h', 'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/host32.py'],
    basis=['INPUT_KEYMAP_DESIGN H0b'], kind='contract')
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
#include "in2_int.h"
#define TYPE(expr, t) _Static_assert(__builtin_types_compatible_p(__typeof__(expr), t), #expr)
TYPE(&pc98_jp_backend, const KmBackend *);
TYPE(&hid_jp_backend, const KmBackend *);
TYPE(&km_source_lookup, int (*)(u8, u16, KmSource *));
TYPE(&km_source_unbind, void (*)(u8, u16));
TYPE(&km_source_release, void (*)(u8, u16));
TYPE(&km_eval, int (*)(const KmBackend *, u16, const KmState *, KmEvalResult *));
TYPE(&km_state_apply, int (*)(const KmKey *, u8, u8, KmEffect));
TYPE(&km_state_snapshot, void (*)(KmState *));
TYPE(&km_state_release, void (*)(u8, u16));
TYPE(&km_state_cancel_tap, void (*)(u8, u16));
TYPE(((KmHidExtensions *)0)->order, const u8 *);
TYPE(((KmHidExtensions *)0)->actions, const u8 (*)[2]);
TYPE(((KmHidExtensions *)0)->action_count, u8);
TYPE(((KmHidExtensions *)0)->no_keydata, const u8 *);
_Static_assert(sizeof(KmHidExtensions) == 16, "HID descriptor size");
_Static_assert(__builtin_offsetof(KmHidExtensions, order) == 0, "order offset");
_Static_assert(__builtin_offsetof(KmHidExtensions, actions) == 4, "actions offset");
_Static_assert(__builtin_offsetof(KmHidExtensions, action_count) == 8, "count offset");
_Static_assert(__builtin_offsetof(KmHidExtensions, no_keydata) == 12, "bitmap offset");
TYPE(&km_release_keys, void (*)(u8, u16));
TYPE(((KmPc98Extensions *)0)->bank0, const KmExactCell (*)[8]);
TYPE(((KmPc98Extensions *)0)->bank1, const KmExactCell (*)[8]);
TYPE(((KmPc98Extensions *)0)->flags, const u8 *);
TYPE(((KmPc98Extensions *)0)->accents, const KmAccent *);
TYPE(((KmSource *)0)->map, const KmBackend *);
TYPE(((KmSource *)0)->map_generation, u32);
TYPE(((KmPc98Extensions *)0)->accent_count, u8);
TYPE(((KmState *)0)->mods, u16); TYPE(((KmState *)0)->locks, u16);
TYPE(((KmEvalResult *)0)->sym, keysym_t);
TYPE(((KmEvalResult *)0)->command, keysym_t);
TYPE(((KmEvalResult *)0)->kind, u8); TYPE(((KmEvalResult *)0)->op, u8);
TYPE(((KmEvalResult *)0)->compat_sc, u8);
TYPE(((KmEvalResult *)0)->is_modifier, u8);
TYPE(((KmEvalResult *)0)->no_keydata, u8);
TYPE(((KmEvalResult *)0)->action, u8);
TYPE(((KmEvalResult *)0)->effect, KmEffect);
_Static_assert(sizeof(KmEffect) == 2, "saved effect");
_Static_assert(sizeof(KmExactCell) == 4, "exact cell");
_Static_assert(__builtin_offsetof(KmExactCell, kind) == 2, "kind offset");
_Static_assert(__builtin_offsetof(KmExactCell, op) == 3, "op offset");
static unsigned seen, bad;
void test_release(u8 src, u16 gen) {
    u8 kind, origin;
    if (!input_src_info(src, gen, &kind, &origin) || seen) bad = 1;
    seen = 1;
}
void input_state_release(u8 src, u16 gen) {
    u8 kind, origin;
    if (seen != 1 || !input_src_info(src, gen, &kind, &origin)) bad = 1;
    seen = 2;
}
int check(void) {
    u16 gen;
    u8 kind, origin;
    int src = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_NATIVE, &gen);
    if (KM_CHAR != 0 || KM_BSD_SPECIAL != 1 || KM_LEGACY_CONTROL != 2 ||
        KM_CELL_INHERIT != 255 || KM_MOD_COMMAND != 0x10 || KM_MOD_META != 0x20 ||
        KM_EFFECT_SHIFTA != 0x2000 || KM_LOCK_ALT != 0x1000 || KM_MAP_PC98 != 1 || KM_MAP_HID != 2) return 1;
    input_src_release(0, gen);
    input_src_release(src, gen + 1);
    if (seen) return 2;
    input_src_release(src, gen);
    if (seen != 2 || bad || input_src_info(src, gen, &kind, &origin)) return 3;
    input_src_release(src, gen);
    return bad ? 4 : 0;
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
    header = (ctx.root / 'input/hal/keymap.h').read_text()
    with tempfile.TemporaryDirectory(prefix='input-keymap-decl-') as td:
        tmp = Path(td)
        (tmp / 'test.c').write_text(SOURCE)
        (tmp / 'in2_int.h').write_text((ctx.root / 'input/hal/in2_int.h').read_text())
        (tmp / 'io.h').write_text('static inline unsigned irq_save(void) { return 0; }\nstatic inline void irq_restore(unsigned f) { (void)f; }\n')
        product = (ctx.root / 'input/hal/in2_dev.c').read_text()
        (tmp / 'in2_dev.c').write_text(product)
        def build(h):
            # Instrument only the empty reservation, to observe the product call order.
            anchor = '    (void)src;\n    (void)generation;'
            assert h.count(anchor) == 1
            (tmp / 'keymap.h').write_text(h.replace(anchor,
                '    extern void test_release(u8, u16);\n    test_release(src, generation);'))
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                            '-nostdlib', '-static', '-no-pie', '-I'+str(tmp),
                            '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32'),
                            str(tmp/'in2_dev.c'), str(tmp/'test.c'),
                            '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert build(header) == 0
        print('PASS x86 H0b declarations, IDs and release order', flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-gcc'
        if cc.is_file() and os.access(cc, os.X_OK):
            subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-DPPC_COMPILE', '-I'+str(tmp),
                            '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32'), '-c', str(tmp/'test.c'),
                            '-o', str(tmp/'test.o')], check=True)
            print('PASS PowerPC declaration cross compile', flush=True)
        else:
            print('SKIP PowerPC cross compile: toolchain unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1': return 1
        if ctx.mutate:
            old = 'KM_CELL_INHERIT = 255'
            assert header.count(old) == 1
            assert build(header.replace(old, 'KM_CELL_INHERIT = 254')) == 1
            print('RED runtime: bank1 inherit kind', flush=True)
            old = 'KM_EFFECT_SHIFTA = 0x2000'
            assert header.count(old) == 1
            assert build(header.replace(old, 'KM_EFFECT_SHIFTA = 0x1000')) == 1
            print('RED runtime: SHIFTA tag collides with AltLock', flush=True)
            call = '        km_source_release(src, generation);'
            assert product.count(call) == 1
            (tmp / 'in2_dev.c').write_text(product.replace(call, ''))
            assert build(header) == 3
            print('RED runtime: release notification removed', flush=True)
            (tmp / 'in2_dev.c').write_text(product)
            assert build(header) == 0
    return 0

if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
