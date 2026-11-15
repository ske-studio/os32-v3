#!/usr/bin/env python3
"""T_merge: source transitions, shared locks and V86 (freestanding ILP32)."""
CHECK = dict(name='input-merge-host', tier=1, budget=10, needs=[], covers=[
    'input/hal/in2_int.h', 'input/hal/in2_dev.c', 'input/hal/in2_state.c', 'input/hal/kbd_merge.c',
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
    names = ['in2_dev.c', 'in2_state.c', 'kbd_merge.c']
    sources = [(ctx.root / 'input/hal' / name).read_text() for name in names]
    with tempfile.TemporaryDirectory(prefix='input-merge-') as td:
        tmp = Path(td)
        (tmp / 'in2_int.h').write_text((ctx.root / 'input/hal/in2_int.h').read_text())
        (tmp / 'io.h').write_text('extern unsigned test_irq;\nstatic inline unsigned irq_save(void) { unsigned f=test_irq; test_irq=0; return f; }\nstatic inline void irq_restore(unsigned f) { test_irq=f; }\n')
        files = [str(tmp / name) for name in names] + [str(ctx.root / 'tools/tests/input_merge_host.c')]
        flags = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-stack-protector',
                 '-I'+str(tmp), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32')]
        def build(texts):
            for name, source in zip(names, texts): (tmp / name).write_text(source)
            subprocess.run(['gcc', '-m32', '-fno-pie', '-nostdlib', '-static', '-no-pie'] + flags + files + ['-o', str(tmp/'test')], check=True)
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
                ('if (down || cnt[sc] == 0)', 'if (1)'),
                ('if (input_emit_legacy_repeat) input_emit_legacy_repeat', 'if (input_emit_in2) input_emit_in2(e);\n        if (input_emit_legacy_repeat) input_emit_legacy_repeat'),
                ('if (legacy_active()) return;', 'if (0) return;')]:
                assert sources[2].count(old) == 1, old
                rc = build(sources[:2] + [sources[2].replace(old, new)])
                assert 0 < rc < 128, (old, rc)
                print('RED runtime: ' + old, flush=True)
            # Fault injection at unplug only: make the private bit writable by the mutant.
            decl = 'static u8 cnt[128], mechanical[IN2_MAX_DEV], usb_lock;'
            release = 'void input_state_release(u8 src, u16 generation)\n{'
            assert sources[2].count(decl) == sources[1].count(release) == 1
            unplug = [sources[0], sources[1].replace(release, release + '\n    extern u8 usb_lock; usb_lock = 0;'),
                      sources[2].replace(decl, 'static u8 cnt[128], mechanical[IN2_MAX_DEV]; u8 usb_lock;')]
            assert 0 < build(unplug) < 128
            print('RED runtime: unplug clears shared USB locks', flush=True)
            assert build(sources) == 0
    return 0

if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
