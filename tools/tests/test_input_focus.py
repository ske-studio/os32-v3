"""Foreground ownership and IRQ raw delivery, using the product F1 and input HAL."""
CHECK = dict(name='input-focus-host', tier=1, needs=[], budget=15,
    covers=['tools/tests/test_input_focus.py', 'tools/tests/input_focus_host.c',
            'platform/pc98/platform_io.h', 'tools/tests/host_arch/**', 'include/**', 'exec/exec_gui.h', 'exec/exec_hooks.h',
            'kernel/av_focus.c', 'kapi/kapi_in2.c', 'input/hal/*.[ch]', 'input/keymaps/*.c', 'platform/pc98/input_keymap.c', 'sdk/include/os32/**'],
    basis=['docs/decisions/sound/AV_HAL_DESIGN.md §5-3, §5-6'])
from pathlib import Path
import subprocess
import tempfile


def run(ctx):
    sources = ['kernel/av_focus.c', 'kapi/kapi_in2.c'] + ['input/hal/' + name + '.c' for name in
        ('kbd_merge', 'keymap_eval', 'keymap_state', 'keymap_registry', 'legacy_keymap', 'legacy_mode', 'in2_excl', 'in2_reclaim', 'legacy_ring', 'mouse_ext', 'in2_ring', 'in2_dev', 'in2_export')]
    sources += ['input/keymaps/pc98_jp.c', 'input/keymaps/hid_jp.c', 'platform/pc98/input_keymap.c']
    cases = []
    if ctx.mutate:
        cases = [
            ('av_focus.c', 'in2_on_focus(old, owner);', 'in2_on_focus(owner, old);'),
            ('av_focus.c', 'in2_on_focus(old, owner);', 'in2_on_focus(old, old);'),
            ('in2_excl.c', '    acq_owner = owner;', '    if (owner) acq_owner = owner;'),
            ('in2_excl.c', '    excl = next;', '    if (next) excl = next;'),
            ('in2_excl.c', '    ++gen;', ''),
            ('in2_excl.c', '    kbd_raw_purge_keep();', ''),
            ('in2_excl.c', '    return !!(excl', '    ++focus_gen; return !!(excl'),
            ('in2_reclaim.c', '    in2_excl_release(owner);', ''),
            ('in2_reclaim.c', 'owner == av_focus_owner()', '1'),
            ('in2_reclaim.c', '    in2_ring_clear();', ''),
            ('in2_reclaim.c', '    mouse_ext_read(&x, &y);', '    (void)x; (void)y;'),
            ('in2_reclaim.c', '    mouse_ext_read_wheel();', ''),
            ('in2_reclaim.c', '    if (foreground) in2_reclaim_generation();', ''),
            ('in2_reclaim.c', '    in2_ring_lost(IN2_LOST_FOCUS);', ''),
            ('in2_reclaim.c', '    in2_excl_focus(old);',
             '    legacy_cooked_flush(); in2_excl_focus(old);'),
            ('in2_excl.c', 'else transition(acq_owner, excl, 1);\n    ++focus_gen;',
             'else transition(acq_owner, excl, 1);'),
            ('in2_reclaim.c', 'unsigned f = irq_save();\n    clear_input();',
             'unsigned f = host_arch_if;\n    clear_input();'),
            ('in2_reclaim.c', 'unsigned f = irq_save();\n    int foreground',
             'unsigned f = host_arch_if;\n    int foreground'),
            ('in2_excl.c', 'unsigned f = irq_save();\n    if (in2_excl_release(owner))',
             'unsigned f = host_arch_if;\n    if (in2_excl_release(owner))'),
            ('in2_excl.c', 'unsigned f = irq_save();\n    if (owner != acq_owner',
             'unsigned f = host_arch_if;\n    if (owner != acq_owner'),
            ('in2_excl.c', ' || sc == KEY_TAB', ''),
            ('in2_excl.c', 'if (focus || (excl & ~next & IN2_EXCL_KBD))', 'if (focus)'),
            ('in2_excl.c', '    if (sc >= 128', '    if (is_break && sc < 128) valid[sc] = 0;\n    if (sc >= 128'),
            ('kbd_merge.c', 'if (!down && cnt[sc] == 0)', 'if (!down)'),
            ('kbd_merge.c', '++release_gen[sc];', '(void)sc;'),
            ('legacy_ring.c', ' && priority[from] != RAW_PROTECTED', ''),
            ('legacy_ring.c', 'int limit = KBD_BUF_SIZE - (switch_pending && !finish);',
             'int limit = KBD_BUF_SIZE;'),
            ('legacy_ring.c', 'RAW_NORMAL, RAW_REPEAT, RAW_WM',
             'RAW_REPEAT, RAW_NORMAL, RAW_WM'),
            ('legacy_ring.c', 'finish || (keycode == KEY_TAB && is_break)',
             '(keycode == KEY_TAB && is_break)'),
            ('legacy_ring.c', 'finish || (keycode == KEY_TAB && is_break)', 'finish'),
            ('legacy_ring.c', 'if (repeat) rank = RAW_REPEAT;', 'if (repeat) rank = RAW_WM;'),
            ('legacy_ring.c', 'if (n != needed) return 0;', 'if (n != needed || slots == 2) return 0;'),
            ('legacy_ring.c', 'limit -= !has_stop;', 'limit -= 0;'),
            ('in2_reclaim.c', 'if (foreground) clear_input();\n    in2_excl_release(owner);',
             'in2_excl_release(owner);\n    if (foreground) clear_input();'),
            ('in2_ring.c', 'head = count = 0;\n    pending = 0;', 'head = count = 0;'),
            ('in2_ring.c', 'ctrl_stop_make(&events[i])', '0'),
            ('in2_ring.c', 'i = duplicate;', 'i = head;'),
            ('in2_ring.c', '        --count;\n        ++dropped;', '        return;'),
        ]
        for path in range(4):
            anchor = 'valid[sc] = input_key_pressed(sc);'
            cases.append(('in2_excl.c', anchor,
                f'extern int snapshot_path; if (snapshot_path != {path}) {anchor}'))
        # Each public entry must reach its own provider at runtime.
        import re
        api = (ctx.root / 'kapi/kapi_in2.c').read_text()
        for body in re.findall(r'int in2_\w+\([^)]*\)\n\{.*?\n\}', api, re.S):
            signature, rest = body.split('{', 1)
            cases.append(('kapi_in2.c', body, signature + '{ return OS32_ERR_NOSYS; }'))
            for guard in ('slot >= IN2_MAX_PADS || ', 'max > IN2_RING_CAPACITY || ',
                          '    if (!foreground()) return OS32_ERR_BUSY;'):
                if guard in rest:
                    cases.append(('kapi_in2.c', body, body.replace(guard, '')))
        cases.extend([
            ('in2_dev.c', 'if (!sources[i].used) continue;', ''),
            ('in2_excl.c', 'if (flags & ~(IN2_EXCL_KBD | IN2_EXCL_MOUSE))', 'if (0)'),
            ('in2_excl.c', 'if (!out || !av_focus_is_wm())', 'if (!out)'),
            ('in2_excl.c', 'return owner && owner == acq_owner;', 'return !!acq_owner;'),
        ])
    with tempfile.TemporaryDirectory() as td:
        folder = Path(td)
        gate = '    if (!IN2_PUBLIC_EXCLUSIVE) return OS32_ERR_NOSYS;'
        closed = [('', '', '')]
        if ctx.mutate:
            closed.append(('kapi_in2.c', gate, ''))
        compile_flags = ['gcc', '-m32', '-DHOST_ARCH_SHARED_IF', '-ffreestanding',
            '-DOS32_HOOK_GUI=0', '-fno-pie', '-ffunction-sections', '-Werror=implicit-function-declaration',
            '-Itools/tests/host_arch', '-I.', '-Ikapi', '-Iinput/hal', '-Iinclude', '-Iexec',
            '-Iplatform/pc98', '-Ikernel', '-Isdk/include/os32']
        binary = folder / 'test'
        for flag, variants in [('-DTEST_CLOSED_ACQUIRE', closed),
                               ('-DIN2_PUBLIC_EXCLUSIVE=1', [('', '', ''), *cases])]:
            units = {}
            paths = {}
            for path in [*sources, 'tools/tests/input_focus_host.c']:
                source = ctx.root / path
                obj = folder / (source.stem + '.o')
                subprocess.run([*compile_flags, flag, '-c', source, '-o', obj], cwd=ctx.root, check=True)
                units[source.name] = obj
                paths[source.name] = source
            for name, old, new in variants:
                objects = units.copy()
                if name:
                    text = paths[name].read_text()
                    assert text.count(old) == 1, old
                    source = folder / name
                    source.write_text(text.replace(old, new))
                    objects[name] = folder / 'mutant.o'
                    subprocess.run([*compile_flags, flag, '-c', source, '-o', objects[name]],
                                   cwd=ctx.root, check=True)
                subprocess.run(['gcc', '-m32', '-no-pie', '-nostdlib', '-static', '-Wl,--gc-sections',
                    '-Wl,--wrap=in2_on_focus', '-Wl,--wrap=input_key_pressed', *objects.values(), '-o', binary],
                    cwd=ctx.root, check=True)
                rc = subprocess.run(['qemu-i386', binary]).returncode
                assert (0 < rc <= 47) if name else rc == 0, (name, old, rc)
                print('RED runtime' if name else 'PASS', name, old.splitlines()[0] if old else '', flush=True)
    return 0
