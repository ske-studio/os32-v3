#!/usr/bin/env python3
"""T_merge: source transitions, shared locks and V86 (freestanding ILP32)."""
CHECK = dict(name='input-merge-host', tier=1, budget=10, needs=[], covers=['input/hal/input_context.h', 'input/hal/in2_excl.c', 'include/av_contract.h', 'input/hal/legacy_keymap.c', 'input/keymaps/pc98_jp.c', 'input/hal/keymap_registry.c', 'input/hal/keymap_eval.c', 'input/hal/keymap_state.c',
    'input/hal/in2_int.h', 'input/hal/in2_dev.c', 'input/hal/keymap.h', 'input/hal/in2_state.c', 'input/hal/mouse_ext.c', 'input/hal/pointer_state.c', 'input/hal/kbd_merge.c', 'input/hal/in2_ring.c',
    'include/types.h', 'include/io.h', 'include/kbd.h', 'include/legacy_guest.h',
    'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/input_merge_host.c',
    'tools/tests/test_input_merge.py', 'tools/tests/host32.py'],
    basis=['INPUT_HAL_DESIGN §3', 'INPUT_HAL_DESIGN §8'], kind='contract')
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32
ROOT = Path(__file__).resolve().parents[2]

def run(ctx):
    names = ['in2_dev.c', 'in2_state.c', 'kbd_merge.c', 'in2_ring.c', 'mouse_ext.c', 'pointer_state.c']
    names += ['legacy_keymap.c', 'keymap_registry.c', 'keymap_eval.c', 'keymap_state.c', 'in2_excl.c']
    sources = [(ctx.root / 'input/hal' / name).read_text() for name in names]
    with tempfile.TemporaryDirectory(prefix='input-merge-') as td:
        tmp = Path(td)
        (tmp / "keymap.h").write_text((ctx.root / "input/hal/keymap.h").read_text())
        (tmp / 'in2_int.h').write_text((ctx.root / 'input/hal/in2_int.h').read_text())
        (tmp / 'io.h').write_text('extern unsigned test_irq;\nstatic inline unsigned irq_save(void) { unsigned f=test_irq; test_irq=0; return f; }\nstatic inline void irq_restore(unsigned f) { test_irq=f; }\n')
        files = [str(tmp / name) for name in names] + [str(ctx.root / 'tools/tests/input_merge_host.c')]
        flags = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-ffunction-sections', '-fdata-sections', '-fno-stack-protector',
                 '-I'+str(tmp), '-I'+str(ctx.root/'input/hal'), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32')]
        def build(texts):
            for name, source in zip(names, texts): (tmp / name).write_text(source)
            subprocess.run(['gcc', '-m32', '-fno-pie', '-nostdlib', '-nostdinc', '-static', '-Wl,--gc-sections', '-no-pie'] + flags + files + ['-Wl,--wrap=input_emit_in2', '-Wl,--wrap=input_capture_delivery', '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert build(sources) == 0
        print('PASS x86 T_merge', flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        if cc.is_file() and os.access(cc, os.X_OK):
            for f in files:
                subprocess.run([str(cc)] + flags + ['-DPPC_COMPILE', '-c', f, '-o', str(tmp/(Path(f).stem+'.o'))], check=True)
            print('PASS PowerPC cross compile', flush=True)
        else:
            print('SKIP PowerPC cross compile: toolchain unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1': return 1
        if ctx.mutate:
            for old, new in [
                ('cnt[sc] += down ? 1 : -1;', 'cnt[sc] += down ? 1 : 0;'),
                ('if (down || (old && cnt[sc] == 0))', 'if (1)'),
                ('e->buttons = kbd_shift_state = input_key_mods();\n    }\n    if (down || (old && cnt[sc] == 0))', 'e->buttons = kbd_shift_state = input_key_mods(); if (down) input_emit_in2(e);\n    }\n    if (down || (old && cnt[sc] == 0))'),
                ('if (legacy_active()) return 1;', 'if (0) return 1;')]:
                assert sources[2].count(old) == 1, old
                rc = build(sources[:2] + [sources[2].replace(old, new)] + sources[3:])
                assert 0 < rc < 128, (old, rc)
                print('RED runtime: ' + old, flush=True)
            index = names.index('in2_excl.c')
            old = 'if (focus || (excl & ~next & IN2_EXCL_KBD))'
            assert sources[index].count(old) == 1
            mutant = sources.copy()
            mutant[index] = mutant[index].replace(old, '(void)focus; if (excl & ~next & IN2_EXCL_KBD)')
            assert 0 < build(mutant) < 256
            print('RED runtime: missing owner-change snapshot', flush=True)
            repeat = '.repeat = event->down && old'
            index = names.index('legacy_keymap.c')
            assert sources[index].count(repeat) == 1
            mutant = sources.copy()
            mutant[index] = mutant[index].replace(repeat, '.repeat = 0')
            assert 0 < build(mutant) < 128
            print('RED runtime: unbound repeat cleared', flush=True)
            # Fault injection at unplug only: make the private bit writable by the mutant.
            decl = 'static u16 cnt[128];\nstatic u8 mechanical[IN2_MAX_DEV], usb_lock;'
            release = 'void input_state_release(u8 src, u16 generation)\n{'
            assert sources[2].count(decl) == sources[1].count(release) == 1
            unplug = [sources[0], sources[1].replace(release, release + '\n    extern u8 usb_lock; usb_lock = 0;'),
                      sources[2].replace(decl, 'static u16 cnt[128]; static u8 mechanical[IN2_MAX_DEV]; u8 usb_lock;'), *sources[3:]]
            assert 0 < build(unplug) < 128
            print('RED runtime: unplug clears shared USB locks', flush=True)
            old = '(void)in2_ring_push(event);'
            assert sources[3].count(old) == 1
            assert 0 < build(sources[:3] + [sources[3].replace(old, '(void)event;')] + sources[4:]) < 128
            print('RED runtime: input_emit_in2 empty', flush=True)
            assert build(sources) == 0
    return 0

if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
