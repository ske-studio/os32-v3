#!/usr/bin/env python3
"""S1pt/S1x/S0d: actual capture, pinned legacy exit and release lifecycle."""
CHECK = dict(name='keymap-wire', tier=1, budget=20, needs=[], kind='contract',
    covers=['tools/tests/input_s3_differences.tsv', 'docs/decisions/INPUT_BIND_DESIGN.md', 'tools/tests/input_pc98_compat_host.c', 'tools/tests/kernel_includes.py', 'input/hal/legacy_keymap.c', 'input/keymaps/pc98_jp.c', 'input/hal/keymap*.c', 'input/hal/keymap.h', 'input/hal/in2_int.h',
            'input/hal/in2_dev.c', 'input/hal/kbd_merge.c', 'input/keymaps/*.c',
            'platform/pc98/input_keymap.c', 'tools/kbd_import.py', 'tools/keymap/pc98_s1p.c',
            'tools/tests/keymap_wire_host.c', 'tools/tests/test_keymap_wire.py',
            'tools/tests/host32.py', 'tools/tests/keymap_s3_oracle.py', 'tools/tests/input_pc98_bsd_fixture.kbd', 'tools/tests/test_input_pc98_bsd.py', 'include/**', 'sdk/include/os32/**'],
    basis=['INPUT_KEYMAP_DESIGN §2 §4 §7 §8 H0b'])
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32
from keymap_s3_oracle import c_oracle
ROOT = Path(__file__).resolve().parents[2]

REGISTRATION_HOST = '#include "keymap.h"\n#include "in2_int.h"\n#include "os32_kapi_shared.h"\nconst KmBackend pc98_jp_backend = {0}, hid_jp_backend = {0};\nstatic int stage, calls, allocated, bound, released;\nint km_backend_register(const KmBackend *m) {\n    ++calls;\n    if ((calls == 1 && m != &pc98_jp_backend) || (calls == 2 && m != &hid_jp_backend)) __builtin_trap();\n    return stage == calls ? OS32_ERR_INVAL : 0;\n}\nint input_src_register(u8 kind, u8 origin, u16 *gen) {\n    if (calls != 2 || kind != INPUT_KIND_KBD || origin != INPUT_ORIGIN_NATIVE) __builtin_trap();\n    ++allocated; *gen = 0x1234;\n    return stage == 3 ? OS32_ERR_FULL : 7;\n}\nint km_source_bind(u8 src, u16 gen, u16 map) {\n    if (src != 7 || gen != 0x1234 || map != KM_MAP_PC98) __builtin_trap();\n    ++bound; return stage == 4 ? OS32_ERR_INVAL : 0;\n}\nvoid input_src_release(u8 src, u16 gen) {\n    if (src != 7 || gen != 0x1234) __builtin_trap();\n    ++released;\n}\nvoid _start(void) {\n    int rc = 0; u16 gen;\n    if (pc98_input_keymap_register(0) != OS32_ERR_INVAL || calls) rc = 1;\n    for (stage = 1; stage <= 5 && !rc; ++stage) {\n        calls = allocated = bound = released = 0;\n        int result = pc98_input_keymap_register(&gen);\n        if (result != (stage == 3 ? OS32_ERR_FULL : stage == 5 ? 7 : OS32_ERR_INVAL) ||\n            calls != (stage == 1 ? 1 : 2) || allocated != (stage >= 3) ||\n            bound != (stage >= 4) || released != (stage == 4)) rc = 2 + stage;\n    }\n    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));\n    __builtin_unreachable();\n}\n'

def run(ctx):
    paths = ['input/hal/'+n+'.c' for n in
             ('in2_dev', 'kbd_merge', 'legacy_keymap', 'keymap_registry', 'keymap_eval', 'keymap_state')]
    paths += ['input/keymaps/pc98_jp.c', 'input/keymaps/hid_jp.c', 'platform/pc98/input_keymap.c']
    sources = {p: (ctx.root/p).read_text() for p in paths}
    assert sources['input/keymaps/pc98_jp.c'] == (ctx.root/'tools/keymap/pc98_s1p.c').read_text().replace('s1p_pc98_backend', 'pc98_jp_backend')
    old = subprocess.check_output(['git', 'show', 'eeeb90673:input/hal/kbd_merge.c'], cwd=ctx.root, text=True)
    old = '#include "in2_int.h"\n#include "kbd.h"\n' + old[old.index('static const u8 scancode_to_ascii['):old.index('void input_emit_legacy_repeat')].replace('input_emit_legacy_key(', 'wire_old_emit(')
    with tempfile.TemporaryDirectory(prefix='keymap-wire-') as directory:
        tmp = Path(directory)
        for name in ('keymap.h', 'in2_int.h'):
            (tmp/name).write_text((ctx.root/'input/hal'/name).read_text())
        (tmp/'io.h').write_text('static inline unsigned irq_save(void) { return 0; }\nstatic inline void irq_restore(unsigned f) { (void)f; }\n')
        (tmp/'old.c').write_text(old)
        (tmp/'s3_expected.h').write_text(c_oracle(ctx.root))
        def build(texts):
            files = []
            for path, text in texts.items():
                target = tmp/Path(path).name
                if path.endswith('kbd_merge.c'):
                    anchor = '        if (boot_locks[i] & SHIFT_CAPS)'
                    assert text.count(anchor) == 1
                    text = 'extern unsigned wire_boot_visits;\n' + text.replace(anchor, '        ++wire_boot_visits;\n'+anchor)
                if path.endswith('keymap_state.c'):
                    anchor = '    for (unsigned int i = 0; i < IN2_MAX_DEV; ++i) {'
                    assert text.count(anchor) == 1
                    text = text.replace('void km_state_snapshot(KmState *out)', 'extern unsigned wire_snapshot_calls, wire_snapshot_visits;\nvoid km_state_snapshot(KmState *out)')
                    text = text.replace('    if (!out) return;', '    ++wire_snapshot_calls;\n    if (!out) return;').replace(anchor, anchor+'\n        ++wire_snapshot_visits;')
                text = text.replace('../../input/hal/keymap.h', 'keymap.h').replace('../../input/hal/in2_int.h', 'in2_int.h')
                target.write_text(text); files.append(str(target))
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                            '-static', '-no-pie', '-I'+str(tmp), '-I'+str(ctx.root),
                            '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32'),
                            *files, str(tmp/'old.c'), str(ctx.root/'tools/tests/keymap_wire_host.c'),
                            '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        (tmp/'register_host.c').write_text(REGISTRATION_HOST)
        (tmp/'register.c').write_text(sources['platform/pc98/input_keymap.c'].replace('../../input/hal/keymap.h', 'keymap.h').replace('../../input/hal/in2_int.h', 'in2_int.h'))
        subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                        '-I'+str(tmp), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32'),
                        str(tmp/'register.c'), str(tmp/'register_host.c'), '-o', str(tmp/'register')], check=True)
        assert host32.run([str(tmp/'register')], runner='qemu').returncode == 0
        print('PASS S1x registration order and all four failure exits')
        rc = build(sources); assert rc == 0, rc
        print('PASS 128 keys x 32 physical modifier/lock combinations; XFER/NFER/STOP/Ctrl+STOP, GUI/alias/V86/release')
        print('MEASURE capture: 2 snapshots: 32 state visits + 32 boot-lock visits with 16 live sources; 0 key scans')
        if ctx.mutate:
            for path, anchor, replacement in [
                ('input/hal/kbd_merge.c', 'if (sc < 128 && old != key->down) cnt[sc] += key->down ? 1 : -1;', 'if (sc < 128 && old != key->down) cnt[sc] += key->down ? 1 : 0;'),
                ('input/hal/kbd_merge.c', 'if (!legacy_active()) {', 'if (1) {'),
                ('input/hal/keymap_registry.c', '    km_release_keys(src, generation);', '    (void)src;'),
                ('input/hal/kbd_merge.c', 'old = input_held_key(key->src, key->keycode);', 'old = 0;')]:
                assert sources[path].count(anchor) == 1
                mutant = dict(sources); mutant[path] = mutant[path].replace(anchor, replacement)
                rc = build(mutant); assert 0 < rc < 128, (anchor, rc)
                print('RED runtime:', anchor)
            assert build(sources) == 0
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
