#!/usr/bin/env python3
"""T_pointer: source state, independent deltas and absolute coordinates."""
CHECK = dict(name='input-pointer-host', tier=1, budget=10, needs=[], covers=[
    'input/hal/in2_int.h', 'input/hal/in2_dev.c', 'input/hal/in2_state.c',
    'input/hal/kbd_merge.c', 'input/hal/mouse_ext.c', 'input/hal/pointer_state.c',
    'tools/tests/input_pointer_host.c', 'tools/tests/test_input_pointer.py',
    'tools/tests/host32.py', 'include/types.h', 'include/io.h', 'include/kbd.h',
    'include/legacy_guest.h', 'sdk/include/os32/os32_kapi_shared.h'],
    basis=['INPUT_HAL_DESIGN §3', 'INPUT_HAL_DESIGN §4', 'INPUT_HAL_DESIGN §8'], kind='contract')
import subprocess
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
import host32
ROOT = Path(__file__).resolve().parents[2]

def run(ctx):
    names = ['in2_dev.c', 'in2_state.c', 'kbd_merge.c', 'mouse_ext.c', 'pointer_state.c']
    sources = [(ctx.root / 'input/hal' / n).read_text() for n in names]
    with tempfile.TemporaryDirectory(prefix='input-pointer-') as td:
        tmp = Path(td)
        (tmp/'in2_int.h').write_text((ctx.root/'input/hal/in2_int.h').read_text())
        (tmp/'io.h').write_text('extern unsigned test_irq;\nstatic inline unsigned irq_save(void) { unsigned f=test_irq; test_irq=0; return f; }\nstatic inline void irq_restore(unsigned f) { test_irq=f; }\n')
        files = [str(tmp/n) for n in names] + [str(ctx.root/'tools/tests/input_pointer_host.c')]
        def build(texts):
            for name, source in zip(names, texts): (tmp/name).write_text(source)
            subprocess.run(['gcc', '-m32', '-fno-pie', '-nostdlib', '-nostdinc', '-static', '-no-pie', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-stack-protector', '-I'+str(tmp), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32')] + files + ['-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert build(sources) == 0
        print('PASS T_pointer', flush=True)
        if ctx.mutate:
            for index, old, new in [
                (4, 'mask |= buttons[i]', 'mask ^= buttons[i]'),
                (4, 'buttons[e->src - 1] ^ e->buttons', 'buttons[e->src - 1] & e->buttons'),
                (3, 'in2_x = add32(in2_x, x)', 'in2_x = add32(0, x)'),
                (3, 'seamless != !!marked', 'seamless == !!marked'),
                (4, '(int)x * (xmax - xmin) / 32767', '(int)x * (xmax - xmin) / 32768'),
                (3, 'if (legacy_active()) return;', 'if (0) return;'),
                (1, 'if (!legacy_active()) input_emit_in2(&e);', 'input_emit_in2(&e);'),
                (3, 'return S32_MAX;', 'return 0;'),
                (3, 'return -S32_MAX - 1;', 'return 0;'),
                (3, 'n = 32767;', 'n = 0;'),
                (3, 'n = -32768;', 'n = 0;'),
                (3, 'pointer_absolute_reset();', '(void)0;'),
                (3, 'old_x = old_y = 0; pointer_absolute_reset();', 'pointer_absolute_reset();'),
                (3, 'if (pointer_absolute_delta(x, y, &dx, &dy))', 'if (pointer_absolute_delta(x, y, &dx, &dy) >= 0)'),
                (4, 'prev_x = px; prev_y = py;', 'prev_x = 0; prev_y = 0;'),
                (4, 'px = 639 / 2', 'px = 0')]:
                assert sources[index].count(old) == 1
                mutated = sources.copy(); mutated[index] = mutated[index].replace(old, new)
                rc = build(mutated); assert 0 < rc < 128, (old, rc)
                print('RED runtime: '+old, flush=True)
            assert build(sources) == 0
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
