#!/usr/bin/env python3
"""T_pointer: source state, independent deltas and absolute coordinates."""
CHECK = dict(name='input-pointer-host', tier=1, budget=10, needs=[], covers=['sys/hal/input/legacy_keymap.c', 'sys/input/keymaps/pc98_jp.c', 'sys/hal/input/keymap_registry.c', 'sys/hal/input/keymap_eval.c', 'sys/hal/input/keymap_state.c', 'tools/tests/input_unused_key_host.h',
    'sys/hal/input/in2_int.h', 'sys/hal/input/in2_dev.c', 'sys/hal/input/keymap.h', 'sys/hal/input/in2_state.c',
    'sys/hal/input/kbd_merge.c', 'sys/hal/input/mouse_ext.c', 'sys/hal/input/pointer_state.c',
    'sys/platform/pc98/input/mouse.c', 'sys/hal/input/mouse_pointer.c', 'sys/hal/input/mouse_pointer_contract.h', 'sys/include/kbd_contract.h', 'sys/include/text_screen.h', 'sys/platform/pc98/input/kbd.h', 'tools/tests/input_poll_host.c',
    'sys/dev/video/kcg/kcg.h', 'sys/include/kcg_contract.h', 'sys/dev/char/np2sysp.h', 'sys/include/kprintf.h',
    'tools/tests/input_pointer_host.c', 'tools/tests/test_input_pointer.py',
    'sys/platform/pc98/input/mouse_bus.c', 'sys/platform/pc98/input/mouse_hal.c', 'tools/tests/mouse_bus_host.c', 'sys/platform/pc98/input/mouse.h', 'sys/platform/pc98/pc98.h', 'sys/hal/input/pointer_service.h',
    'tools/tests/host32.py', 'sys/include/types.h', 'sys/include/io.h', 'sys/include/kbd.h',
    'sys/include/legacy_guest.h', 'sdk/include/os32/os32_kapi_shared.h'],
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
    names += ['legacy_keymap.c', 'keymap_registry.c', 'keymap_eval.c', 'keymap_state.c']
    sources = [(ctx.root / 'input/hal' / n).read_text() for n in names]
    with tempfile.TemporaryDirectory(prefix='input-pointer-') as td:
        tmp = Path(td)
        (tmp / "keymap.h").write_text((ctx.root / "sys/hal/input/keymap.h").read_text())
        (tmp/'in2_int.h').write_text((ctx.root/'sys/hal/input/in2_int.h').read_text())
        (tmp/'io.h').write_text('extern unsigned test_irq; extern void test_irq_restore(unsigned);\nstatic inline unsigned irq_save(void) { unsigned f=test_irq; test_irq=0; return f; }\nstatic inline void irq_restore(unsigned f) { test_irq_restore(f); }\nextern unsigned test_inp(unsigned); extern void test_outp(unsigned, unsigned);\n#define inp test_inp\n#define outp test_outp\n')
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
                (tmp/name).write_text(source.replace('../../sys/hal/input/in2_int.h', 'in2_int.h'))
            (tmp/'mouse.h').write_text((ctx.root/'sys/platform/pc98/input/mouse.h').read_text().replace('../../sys/hal/input/pointer_service.h', 'pointer_service.h'))
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
            for rel in ('../../sys/hal/input/in2_int.h', '../video/kcg/kcg.h', '../char/np2sysp.h'):
                source = source.replace(rel, str((ctx.root/'drivers/input'/rel).resolve()))
            return source.replace('#include "idt.h"', 'void irq_enable(u8 irq);').replace('#include "kbd.h"', '#include "'+str(ctx.root/'sys/platform/pc98/input/kbd.h')+'"')
        for n, source in zip(('ref_mouse.c', 'ref_mouse_bus.c'), old):
            extra = '\nvoid ref_mouse_position(i16 *x, i16 *y) { *x = cursor_x; *y = cursor_y; }\n' if n == 'ref_mouse.c' else ''
            (tmp/n).write_text(prefix + adapt_poll(source) + extra)
        # This stage's exact base, independently namespaced for seamless comparison.
        base_mouse = subprocess.check_output(['git', 'show', 'ef67e51c:sys/platform/pc98/input/mouse.c'], cwd=ctx.root, text=True)
        base_state = subprocess.check_output(['git', 'show', 'ef67e51c:sys/hal/input/pointer_state.c'], cwd=ctx.root, text=True)
        base_symbols = set(re.findall(r'^(?:void|int|u8) +(\w+)\(', base_mouse + '\n' + base_state, re.M))
        base_prefix = ''.join('#define '+n+' base_'+n+'\n' for n in sorted(base_symbols))
        base_prefix += '#define mouse_ext_mode ref_mouse_ext_mode\n'
        for n, body in [('base_mouse.c', adapt_poll(base_mouse)), ('base_state.c', base_state)]:
            (tmp/n).write_text(base_prefix + body)
        poll_source = (ctx.root/'sys/platform/pc98/input/mouse.c').read_text()
        pointer_source = (ctx.root/'sys/hal/input/mouse_pointer.c').read_text()
        def build_poll(source, state=sources[4], ext=sources[3], hal=bus_sources[1], pointer=pointer_source):
            for n, body in zip(names, sources): (tmp/n).write_text(body)
            (tmp/'pointer_state.c').write_text(state)
            (tmp/'mouse_ext.c').write_text(ext)
            (tmp/'mouse.c').write_text(adapt_poll(source))
            (tmp/'mouse_pointer.c').write_text(pointer)
            (tmp/'mouse_hal.c').write_text(hal.replace('../../sys/hal/input/in2_int.h', 'in2_int.h'))
            files = names + ['mouse_bus.c', 'mouse_hal.c', 'mouse.c', 'mouse_pointer.c', 'ref_mouse.c', 'ref_mouse_bus.c', 'base_mouse.c', 'base_state.c']
            subprocess.run(['gcc', '-m32', '-fno-pie', '-nostdlib', '-nostdinc', '-static', '-no-pie', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-stack-protector', '-I'+str(tmp), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32')] + [str(tmp/n) for n in files] + [str(ctx.root/'tools/tests/input_poll_host.c'), '-o', str(tmp/'poll')], check=True)
            return host32.run([str(tmp/'poll')], runner='qemu').returncode
        assert build_poll(poll_source) == 0
        if ctx.mutate:
            for before, after in [('mouse_legacy_snapshot(dx, dy, buttons, x, y);',
                                   '(void)x; (void)y; *dx = *dy = 0; *buttons = 0;'),
                                  ('mouse_ext_mode(0);', 'mouse_ext_mode(1);'),
                                  ('mouse_ext_mode(1);', 'mouse_ext_mode(0);')]:
                assert poll_source.count(before) == 1
                rc = build_poll(poll_source.replace(before, after)); assert 0 < rc < 128, (before, rc)
                print('RED runtime poll: '+before, flush=True)
            for before, after in [('backend = b;', '(void)b;'),
                                  ('backend->text_cell(cursor_x, cursor_y, &cx, &cy);', 'cx = cy = 0;')]:
                assert pointer_source.count(before) == 1
                rc = build_poll(poll_source, pointer=pointer_source.replace(before, after))
                assert 0 < rc < 128, (before, rc)
                print('RED runtime backend: '+before, flush=True)
            before = 'pointer_bounds(x_min, y_min, x_max, y_max);'
            assert pointer_source.count(before) == 1
            rc = build_poll(poll_source, pointer=pointer_source.replace(before, '(void)0;'))
            assert 0 < rc < 128, (before, rc)
            print('RED runtime poll: '+before, flush=True)
            for before, after in [('*dx = old_x; *dy = old_y;', '*dx = 0; *dy = 0;'),
                                  ('*buttons = pointer_buttons_locked();', '*buttons = 0;'),
                                  ('pointer_position_locked(x, y);', '*x = 0; *y = 0;')]:
                assert sources[3].count(before) == 1
                rc = build_poll(poll_source, ext=sources[3].replace(before, after))
                assert 0 < rc < 128, (before, rc)
                print('RED runtime snapshot: '+before, flush=True)
            before = "    unsigned f = irq_save();\n    *dx = old_x; *dy = old_y; old_x = old_y = 0;\n    *buttons = pointer_buttons_locked();\n    pointer_position_locked(x, y);\n    irq_restore(f);"
            assert sources[3].count(before) == 1
            after = "    mouse_ext_read_legacy(dx, dy);\n    *buttons = input_buttons();\n    pointer_position(x, y);"
            rc = build_poll(poll_source, ext=sources[3].replace(before, after))
            assert 0 < rc < 128, rc
            print('RED runtime split snapshot', flush=True)
            before = 'xmin = x0; ymin = y0; xmax = x1; ymax = y1;'
            assert sources[4].count(before) == 1
            rc = build_poll(poll_source, sources[4].replace(before, 'if (x0 > x1 || y0 > y1) { return; } '+before))
            assert 0 < rc < 128, rc
            print('RED runtime reversed bounds', flush=True)
            for before, after in [('mouse_ext_sample(seamless_hal_src, seamless_hal_generation, x, y, time_ms);', '(void)0;'),
                                  ('int sampled = seamless_mouse_poll(&x, &y);', 'int sampled = seamless_mouse_poll(&x, &y) && seamless_mouse_poll(&x, &y);'),
                                  ('u8 buttons = mouse_read_buttons();', 'u8 buttons = 0;')]:
                assert bus_sources[1].count(before) == 1
                rc = build_poll(poll_source, hal=bus_sources[1].replace(before, after))
                assert 0 < rc < 128, (before, rc)
                print('RED runtime seamless: '+before, flush=True)
            before = '        int sampled = seamless_mouse_poll(&x, &y);\n        u8 buttons = mouse_read_buttons();\n        unsigned f = irq_save();'
            assert bus_sources[1].count(before) == 1
            for after in ['        unsigned f = irq_save();\n        int sampled = seamless_mouse_poll(&x, &y);\n        u8 buttons = mouse_read_buttons();',
                          '        int sampled = seamless_mouse_poll(&x, &y);\n        unsigned f = irq_save();\n        u8 buttons = mouse_read_buttons();']:
                rc = build_poll(poll_source, hal=bus_sources[1].replace(before, after))
                assert 0 < rc < 128, rc
                print('RED runtime seamless I/O under lock', flush=True)
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
                (1, 'if (!legacy_active()) mouse_ext_wheel(e.x);', '(void)0;'),
                (1, 'if (!legacy_active()) mouse_ext_wheel(e.x);', 'mouse_ext_wheel(e.x);'),
                (4, 'xmax = 639, ymax = 399', 'xmax = 640, ymax = 400'),
                (4, 'set_bounds_locked(0, 0, x_max, y_max);', 'set_bounds_locked(0, 0, y_max, x_max);'),
                (4, 'app_bounds_set = 1;', 'app_bounds_set = 0;'),
                (4, 'seamless = !!enabled;', 'seamless = !enabled;'),
                (4, 'mask |= buttons[i]', 'mask ^= buttons[i]'),
                (4, 'buttons[e->src - 1] ^ e->buttons', 'buttons[e->src - 1] & e->buttons'),
                (3, 'in2_x = add32(in2_x, x)', 'in2_x = add32(0, x)'),
                (3, 'pointer_seamless() != !!marked', 'pointer_seamless() == !!marked'),
                (4, 'int nx = xmin + (int)x * (xmax - xmin) / 32767;', 'int nx = xmin + (int)x * (xmax - xmin) / 32768;'),
                (3, 'if (legacy_active()) return;', 'if (0) return;'),
                (1, 'if (!legacy_active()) input_emit_in2(&e);', 'input_emit_in2(&e);'),
                (3, 'return S32_MAX;', 'return 0;'),
                (3, 'return -S32_MAX - 1;', 'return 0;'),
                (3, 'n = 32767;', 'n = 0;'),
                (3, 'n = -32768;', 'n = 0;'),
                (4, 'pointer_absolute_reset();', '(void)0;'),
                (4, 'mouse_ext_read_legacy(&dx, &dy);', '(void)dx; (void)dy;'),
                (3, 'if (pointer_absolute_delta(x, y, &dx, &dy))', 'if (pointer_absolute_delta(x, y, &dx, &dy) >= 0)'),
                (4, 'prev_x = nx; prev_y = ny;', 'prev_x = 0; prev_y = 0;'),
                (4, 'px = 639 / 2', 'px = 0')]:
                assert sources[index].count(old) == 1
                mutated = sources.copy(); mutated[index] = mutated[index].replace(old, new)
                rc = build(mutated); assert 0 < rc < 128, (old, rc)
                print('RED runtime: '+old, flush=True)
            assert build(sources) == 0
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
