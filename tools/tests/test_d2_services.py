"""D2 A: real input table, all public wrappers, wiring and ABI stability."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]


def run(runner, mutation=None):
    service = (ROOT / 'kernel/input_service.c').read_text()
    bind = (ROOT / 'platform/pc98/d2_bind.c').read_text()
    wrappers = (ROOT / 'kapi/kapi_generated.c').read_text()
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    entries = [(i, e) for i, e in enumerate(spec['api'])
               if e.get('target', '').startswith('input_')]
    expected = {
        'kbd_getchar': ('', '107', '7', '-1'),
        'kbd_getkey': ('', '108', '8', '-1'),
        'kbd_trygetchar': ('', '103', '3', '-1'),
        'kbd_trygetkey': ('', '104', '4', '-1'),
        'kbd_peekkey': ('', '105', '5', '-1'),
        'kbd_trygetchar_local': ('', '106', '6', '-1'),
        'kbd_is_pressed': ('KEY_CTRL', '111', '11', '0'),
        'kbd_trygetrawkey': ('', '112', '12', '-1'),
        'kbd_get_modifiers': ('', '0x87654321u', '13', '0'),
        'kbd_dropped_count': ('', '0xfedcba98u', '14', '0'),
        'kbd_diag': ('&diag', '-18', '18', 'OS32_ERR_NOSYS'),
        'kbd_diag_log': ('0xff123456u, &log, -3', '-19', '19', 'OS32_ERR_NOSYS'),
    }
    assert {e['name'] for _, e in entries} == set(expected)
    selected, checks = [], []
    for slot, entry in entries:
        name = entry['name']
        match = re.search(r'^(?:int|u32) __cdecl wrap_' + name +
                          r'\([^\n]*\)\n\{.*?^\}', wrappers, re.M | re.S)
        assert match, name
        selected.append(match[0])
        args, result, seen, _ = expected[name]
        checks.append(f'CHECK(wrap_{name}({args}) == {result} && seen == {seen} && hit == {slot});')
    checks += ['CHECK(sequence == 0xff123456u && output == &log && arg == -3);',
               'input_register(&empty_input);']
    for slot, entry in entries:
        args, _, _, result = expected[entry['name']]
        checks.append(f'CHECK(wrap_{entry["name"]}({args}) == {result} && hit == {slot});')
    pointer_selected, pointer_checks = [], []
    pointer_calls = {
        'mouse_poll': ('&state', 'pointer_seen == 1 && pointer_output == &state && state.x == -123 && state.y == 456 && state.dx == 7 && state.dy == -8 && state.buttons == (MOUSE_BTN_LEFT | MOUSE_BTN_RIGHT) && state.mode == MOUSE_MODE_SEAMLESS'),
        'mouse_available': ('', 'pointer_seen == 2'),
        'mouse_set_bounds': ('-31, -42, 513, 624', 'pointer_seen == 3 && pointer_bounds[0] == -31 && pointer_bounds[1] == -42 && pointer_bounds[2] == 513 && pointer_bounds[3] == 624'),
        'mouse_cursor_set_mode': ('MOUSE_CURSOR_TEXT', 'pointer_seen == 4 && pointer_mode == MOUSE_CURSOR_TEXT'),
        'mouse_cursor_show': ('', 'pointer_seen == 5'),
        'mouse_cursor_hide': ('', 'pointer_seen == 6'),
    }
    for name, (call_args, check) in pointer_calls.items():
        slot = next(i for i, e in enumerate(spec['api']) if e['name'] == name)
        entry = spec['api'][slot]
        assert entry.get('target', entry.get('body', '')).startswith('pointer_'), name
        match = re.search(r'^(?:int|void) __cdecl wrap_' + name +
                          r'\([^\n]*\)\n\{.*?^\}', wrappers, re.M | re.S)
        assert match, name
        pointer_selected.append(match[0])
        call = f'wrap_{name}({call_args})'
        pointer_checks.append((f'CHECK({call} == 1);' if name == 'mouse_available' else call + ';') +
                              f'CHECK({check} && hit == {slot});')
    pointer_checks += ['pointer_register(&empty_pointer);',
                       'wrap_mouse_poll(&state);',
                       'CHECK(!state.x && !state.y && !state.dx && !state.dy && !state.buttons && !state.mode);',
                       'CHECK(!wrap_mouse_available());',
                       'wrap_mouse_set_bounds(-1, -2, 3, 4);',
                       'wrap_mouse_cursor_set_mode(MOUSE_CURSOR_NONE);',
                       'wrap_mouse_cursor_show(); wrap_mouse_cursor_hide();']
    if mutation == 'unbound':
        assert 'input_register(&pc98_input);' in bind
        bind = bind.replace('input_register(&pc98_input);', '')
    elif mutation == 'shift':
        assert 'return kbd_shift_state;' in bind
        bind = bind.replace('return kbd_shift_state;', 'return 0;')
    elif mutation == 'pointer_bounds':
        old = 'pointer_provider->bounds(x_min, y_min, x_max, y_max)'
        assert old in service
        service = service.replace(old, 'pointer_provider->bounds(y_min, x_min, y_max, x_max)')
    elif mutation == 'target':
        assert 'input_getchar_kapi()' in selected[1]
        selected[1] = selected[1].replace('input_getchar_kapi()', 'kbd_getchar_kapi()')
    with tempfile.TemporaryDirectory(prefix='d2-input-') as directory:
        work = Path(directory)
        fixture = (ROOT / 'tools/tests/d2_services_host.c').read_text()
        fixture = fixture.replace('#include "../../kernel/input_service.c"', service)
        fixture = fixture.replace('#include "../../platform/pc98/d2_bind.c"', bind)
        fixture = fixture.replace('#include "../../drivers/kbd.h"', '#include "' + str(ROOT / 'drivers/kbd.h') + '"')
        fixture = fixture.replace('#include "../../drivers/mouse.h"', '#include "' + str(ROOT / 'drivers/mouse.h') + '"')
        fixture = fixture.replace('#include \"pointer_fixture_host.h\"', '#include \"' + str(ROOT / 'tools/tests/pointer_fixture_host.h') + '\"')
        (work / 'fixture.c').write_text(fixture)
        helper = wrappers[wrappers.index('#define KAPI_OUT_LEN('):wrappers.index('static int kapi_in_range(')]
        (work / 'input_wrappers.inc').write_text(helper + '\n'.join(selected))
        (work / 'input_wrapper_checks.inc').write_text('\n'.join(checks))
        shift = re.search(r'^u8 fep_kbd_shift_state\(void\).*$',
                          (ROOT / 'kernel/fep_module.c').read_text(), re.M)
        assert shift
        (work / 'input_shift.inc').write_text(shift[0])
        (work / 'pointer_wrappers.inc').write_text('\n'.join(pointer_selected))
        (work / 'pointer_wrapper_checks.inc').write_text('\n'.join(pointer_checks))
        exe = work / 'test'
        includes = ['-I' + str(ROOT / p) for p in ('include', 'sdk/include/os32')]
        subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-Wno-unused-const-variable',
                        '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                        '-nostdlib', '-static', '-no-pie', *includes,
                        str(work / 'fixture.c'), '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner=runner, timeout=30)
        assert result.returncode == (1 if mutation else 0), result.returncode
    print('PASS D2 A input/pointer' + (' RED ' + mutation if mutation else ' (25 operations / 18 wrappers)'))


def contracts():
    boot = (ROOT / 'kernel/kernel.c').read_text()
    assert re.search(r'kstack_hw_init\(\);\s*pc98_d2_bind\(\);', boot)
    assert boot.index('pc98_d2_bind();') < boot.index('kbd_init();')
    for name in ('fs/vfs_fd.c', 'exec/exec.c', 'kernel/gui.c', 'kernel/fep_module.c'):
        source = (ROOT / name).read_text()
        assert '#include "input_service.h"' in source
        assert not re.search(r'#include.*drivers/(kbd|mouse)\.h', source)
        code = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
        assert not re.search(r'\bkbd_(?:getchar|getkey|set_gui_mode|discard_stop)\s*\(', code)
    for name in ('kernel/ime.c', 'kdebug/kselftest.c', 'kernel/kernel.c'):
        assert '#include "../drivers/kbd.h"' in (ROOT / name).read_text()
    assert '#include "../drivers/mouse.h"' in boot
    assert '#include "../include/pointer_service.h"' in (ROOT / 'drivers/mouse.h').read_text()
    assert 'drivers/' not in (ROOT / 'include/pointer_service.h').read_text()
    common = (ROOT / 'include/kbd.h').read_text()
    assert 'drivers/' not in common and 'kbd_init' not in common and 'KBD_DATA' not in common
    assert '#include "../include/kbd.h"' in (ROOT / 'drivers/kbd.h').read_text()
    # The common header must resolve without the drivers include search path.
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-Werror', '-fsyntax-only', '-x', 'c',
                    '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'sdk/include/os32'), '-'],
                   input='#include "input_service.h"\n#include "pointer_service.h"\n', text=True, check=True)
    with tempfile.TemporaryDirectory(prefix='d2-symbols-') as directory:
        obj = Path(directory) / 'input_service.o'
        subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie',
                        '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'sdk/include/os32'),
                        '-c', str(ROOT / 'kernel/input_service.c'), '-o', str(obj)], check=True)
        symbols = subprocess.check_output(['nm', '-u', str(obj)], text=True)
        assert not symbols.strip(), symbols
    print('PASS D2 A dependency, symbols and boot wiring contracts')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    contracts()
    run(args.runner)
    if args.mutate:
        for mutation in ('unbound', 'shift', 'target', 'pointer_bounds'):
            run(args.runner, mutation)
