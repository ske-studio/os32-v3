#!/usr/bin/env python3
"""T_pointer: source state, independent deltas and absolute coordinates."""
CHECK = dict(name='input-pointer-host', tier=1, budget=10, needs=[], covers=[
    'input/hal/in2_int.h', 'input/hal/in2_dev.c', 'input/hal/in2_state.c',
    'input/hal/kbd_merge.c', 'input/hal/mouse_ext.c', 'input/hal/pointer_state.c',
    'drivers/input/mouse.c', 'drivers/input/kbd.h', 'tools/tests/input_poll_host.c',
    'drivers/video/kcg/kcg.h', 'include/kcg_contract.h', 'drivers/char/np2sysp.h', 'include/kprintf.h',
    'tools/tests/input_pointer_host.c', 'tools/tests/test_input_pointer.py',
    'drivers/input/mouse_bus.c', 'drivers/input/mouse_hal.c', 'tools/tests/mouse_bus_host.c', 'drivers/input/mouse.h', 'include/pc98.h', 'include/pointer_service.h',
    'tools/tests/host32.py', 'include/types.h', 'include/io.h', 'include/kbd.h',
    'include/legacy_guest.h', 'sdk/include/os32/os32_kapi_shared.h'],
    basis=['INPUT_HAL_DESIGN §3', 'INPUT_HAL_DESIGN §4', 'INPUT_HAL_DESIGN §8'], kind='contract')
import re
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
        (tmp/'io.h').write_text('extern unsigned test_irq;\nstatic inline unsigned irq_save(void) { unsigned f=test_irq; test_irq=0; return f; }\nstatic inline void irq_restore(unsigned f) { test_irq=f; }\nextern unsigned test_inp(unsigned); extern void test_outp(unsigned, unsigned);\n#define inp test_inp\n#define outp test_outp\n')
        files = [str(tmp/n) for n in names] + [str(ctx.root/'tools/tests/input_pointer_host.c')]
        def build(texts):
            for name, source in zip(names, texts): (tmp/name).write_text(source)
            subprocess.run(['gcc', '-m32', '-fno-pie', '-nostdlib', '-nostdinc', '-static', '-no-pie', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-stack-protector', '-I'+str(tmp), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32')] + files + ['-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert build(sources) == 0
        bus_sources = [(ctx.root/'drivers/input'/n).read_text() for n in ('mouse_bus.c', 'mouse_hal.c')]
        def build_bus(texts):
            for name, source in zip(names, sources): (tmp/name).write_text(source)
            for name, source in zip(('mouse_bus.c', 'mouse_hal.c'), texts):
                (tmp/name).write_text(source.replace('../../input/hal/in2_int.h', 'in2_int.h'))
            (tmp/'mouse.h').write_text((ctx.root/'drivers/input/mouse.h').read_text().replace('../../include/pointer_service.h', 'pointer_service.h'))
            subprocess.run(['gcc', '-m32', '-fno-pie', '-nostdlib', '-nostdinc', '-static', '-no-pie', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-stack-protector', '-I'+str(tmp), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32')] + [str(tmp/n) for n in names + ['mouse_bus.c', 'mouse_hal.c']] + [str(ctx.root/'tools/tests/mouse_bus_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert build_bus(bus_sources) == 0
        # Unmodified base bodies, with only includes/symbols adapted for host linkage.
        old = [subprocess.check_output(['git', 'show', '3fde1a3d4:drivers/input/'+n], cwd=ctx.root, text=True)
               for n in ('mouse.c', 'mouse_bus.c')]
        symbols = set(re.findall(r'^(?:void|int|u8) +(\w+)\(', '\n'.join(old), re.M))
        symbols.update(('mouse_hal_init', 'mouse_hal_capture', 'bus_mouse_get'))
        prefix = ''.join('#define '+n+' ref_'+n+'\n' for n in sorted(symbols))
        def adapt_poll(source):
            for rel in ('../../input/hal/in2_int.h', '../video/kcg/kcg.h', '../char/np2sysp.h'):
                source = source.replace(rel, str((ctx.root/'drivers/input'/rel).resolve()))
            return source.replace('#include "idt.h"', 'void irq_enable(u8 irq);').replace('#include "kbd.h"', '#include "'+str(ctx.root/'drivers/input/kbd.h')+'"')
        for n, source in zip(('ref_mouse.c', 'ref_mouse_bus.c'), old):
            extra = '\nvoid ref_mouse_position(i16 *x, i16 *y) { *x = cursor_x; *y = cursor_y; }\n' if n == 'ref_mouse.c' else ''
            (tmp/n).write_text(prefix + adapt_poll(source) + extra)
        poll_source = (ctx.root/'drivers/input/mouse.c').read_text()
        def build_poll(source, state=sources[4]):
            for n, body in zip(names, sources): (tmp/n).write_text(body)
            (tmp/'pointer_state.c').write_text(state)
            (tmp/'mouse.c').write_text(adapt_poll(source))
            files = names + ['mouse_bus.c', 'mouse_hal.c', 'mouse.c', 'ref_mouse.c', 'ref_mouse_bus.c']
            subprocess.run(['gcc', '-m32', '-fno-pie', '-nostdlib', '-nostdinc', '-static', '-no-pie', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-stack-protector', '-I'+str(tmp), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32')] + [str(tmp/n) for n in files] + [str(ctx.root/'tools/tests/input_poll_host.c'), '-o', str(tmp/'poll')], check=True)
            return host32.run([str(tmp/'poll')], runner='qemu').returncode
        assert build_poll(poll_source) == 0
        if ctx.mutate:
            for before, after in [('mouse_ext_read_legacy(&dx, &dy);', 'dx = dy = 0;'),
                                  ('buttons = input_buttons();', 'buttons = 0;'),
                                  ('pointer_position(&cursor_x, &cursor_y);\n\n', 'cursor_x += dx; cursor_y += dy;\n\n'),
                                  ('pointer_bounds(x_min, y_min, x_max, y_max);', '(void)0;')]:
                assert poll_source.count(before) == 1
                rc = build_poll(poll_source.replace(before, after)); assert 0 < rc < 128, (before, rc)
                print('RED runtime poll: '+before, flush=True)
            before = 'xmin = x0; ymin = y0; xmax = x1; ymax = y1;'
            assert sources[4].count(before) == 1
            rc = build_poll(poll_source, sources[4].replace(before, 'if (x0 > x1 || y0 > y1) { irq_restore(f); return; } '+before))
            assert 0 < rc < 128, rc
            print('RED runtime reversed bounds', flush=True)
            assert build_poll(poll_source) == 0
        print('PASS T_pointer', flush=True)
        if ctx.mutate:
            for index, old, new in [(0, 'mouse_hal_capture(dx, dy, bus_btn_raw);', 'mouse_hal_capture(dx - dx, dy, bus_btn_raw);'), (1, 'e.buttons |= MOUSE_BTN_LEFT', 'e.buttons |= MOUSE_BTN_RIGHT'), (1, 'bus_hal_src = src > 0 ? (u8)src : 0;', 'bus_hal_src = src > 0 ? (u8)src : 1;')]:
                assert bus_sources[index].count(old) == 1
                mutated = bus_sources.copy(); mutated[index] = mutated[index].replace(old, new)
                rc = build_bus(mutated); assert 0 < rc < 128, (old, rc)
                print('RED runtime: '+old, flush=True)
            assert build_bus(bus_sources) == 0
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
