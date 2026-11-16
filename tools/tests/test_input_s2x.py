#!/usr/bin/env python3
"""S2x real old/new capture trace comparison and startup fault injection."""
CHECK = dict(name='input-s2x-host', tier=1, budget=45, needs=[], kind='characterization',
    basis=['INPUT_KEYMAP_DESIGN §7 S2x, S2x へ送る項目'], covers=[
    'drivers/input/kbd_hal.c', 'drivers/input/kbd.c', 'drivers/input/*.h',
    'platform/pc98/input_keymap.c', 'input/keymaps/*.c', 'input/hal/*.c', 'input/hal/*.h',
    'include/**', 'sdk/include/os32/**', 'tools/tests/host_arch/**', 'platform/pc98/platform_io.h',
    'build/config.mk', 'tools/tests/kernel_includes.py', 'tools/tests/host32.py',
    'tools/tests/test_input_s2x.py', 'tools/tests/input_s2x_host.c', 'tools/tests/test_input_native.py',
    'tools/tests/input_native_host.c'])
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32
from kernel_includes import include_flags
from test_input_native import function
ROOT = Path(__file__).resolve().parents[2]
BASE = '08e024265'
HAL = 'drivers/input/kbd_hal.c'
MERGE = 'input/hal/kbd_merge.c'
PATHS = [HAL, MERGE, 'platform/pc98/input_keymap.c', 'input/keymaps/pc98_jp.c',
         'input/keymaps/hid_jp.c'] + ['input/hal/' + n + '.c' for n in
         ('in2_dev', 'in2_state', 'in2_ring', 'mouse_ext', 'pointer_state',
          'legacy_ring', 'legacy_mode', 'keymap_registry', 'keymap_eval', 'keymap_state')]
WRAP = ['km_backend_register', 'input_src_register', 'km_source_bind', 'input_capture',
        'input_capture_key', 'km_eval']

def run(ctx):
    root = ctx.root
    sources = {p: (root / p).read_text() for p in PATHS}
    old = dict(sources)
    for p in (HAL, MERGE):
        old[p] = subprocess.check_output(['git', 'show', BASE + ':' + p], cwd=root, text=True)
    with tempfile.TemporaryDirectory(prefix='input-s2x-') as td:
        tmp = Path(td)
        (tmp / 's2x_deliver.inc').write_text(function((root/'drivers/input/kbd.c').read_text(), 'kbd_deliver'))
        def build(texts, name, previous=False):
            files = []
            for i, (p, source) in enumerate(texts.items()):
                def relocate(m):
                    target = root / Path(p).parent / m[1]
                    return '#include "' + str(target.resolve()) + '"' if target.is_file() else m[0]
                path = tmp / (str(i) + '.c')
                path.write_text(re.sub(r'^#include "([^"]+)"', relocate, source, flags=re.M))
                files.append(str(path))
            exe = tmp / name
            cmd = ['gcc', '-m32', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdinc', '-nostdlib',
                   '-static', '-no-pie', '-I'+str(tmp), '-I'+str(root/'tools/tests/host_arch'),
                   '-I'+str(root/'input/hal'), '-I'+str(root), *include_flags(root)]
            if previous: cmd.append('-DS2X_OLD')
            subprocess.run(cmd + files + [str(root/'tools/tests/input_s2x_host.c')] +
                           ['-Wl,--wrap='+n for n in WRAP] + ['-o', str(exe)], check=True)
            return exe
        def trace(exe, lock, fault):
            r = host32.run([str(exe)], runner='qemu', input=bytes([lock, fault]), capture_output=True)
            return r.returncode, r.stdout
        previous = build(old, 'old', True)
        current = build(sources, 'new')
        expected = {}
        for lock in (0, 2, 4, 6):  # SHIFT_CAPS=2, SHIFT_KANA=4
            rc, expected[lock] = trace(previous, lock, 0)
            assert rc == 0, ('old', lock, rc)
            for fault in range(5):
                rc, actual = trace(current, lock, fault)
                assert rc == 0, (lock, fault, rc)
                assert actual == expected[lock], (lock, fault, 'trace mismatch',
                    next((i for i, (a,b) in enumerate(zip(actual, expected[lock])) if a != b), None))
        print('PASS S2x: 128 keys x 32 modifiers x 4 boot locks; CUI/raw/repeat/V86/STOP; both backend, source, bind failures', flush=True)
        if ctx.mutate:
            mutations = [
                ('.keycode = scancode & SCANCODE_KEY', '.keycode = scancode'),
                ('.keycode = scancode & SCANCODE_KEY, .down = !(scancode & SCANCODE_BREAK)',
                 '.keycode = scancode & SCANCODE_KEY, .down = !!(scancode & SCANCODE_BREAK)'),
                ('(void)input_capture_key(&key);', '(void)key;'),
                ('if (!kbd_native_keymap)', 'if (0 && !kbd_native_keymap)'),
            ]
            for i, (anchor, replacement) in enumerate(mutations):
                assert sources[HAL].count(anchor) == 1, anchor
                texts = dict(sources); texts[HAL] = texts[HAL].replace(anchor, replacement)
                mutant = build(texts, 'mutant')
                fault = 1 if i == 3 else 0
                rc, actual = trace(mutant, 0, fault)
                assert rc >= 0 and (rc or actual != expected[0]), (anchor, rc)
                print('RED runtime:', anchor, 'rc=', rc, flush=True)
            rc, actual = trace(current, 0, 0)
            assert rc == 0 and actual == expected[0]
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
