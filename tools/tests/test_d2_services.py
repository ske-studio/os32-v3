"""D2 A/B/C: real service tables, public wrappers, wiring and ABI stability."""
from kernel_includes import include_flags
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
    port_selected, port_checks = [], []
    port_calls = {
        'serial_init': ('0xfedcba98u', 'port_seen == 1 && port_baud == 0xfedcba98ul', None),
        'serial_puts': ('"port"', "port_seen == 2 && port_string[0] == 'p' && port_string[4] == 0", None),
        'serial_getchar': ('', 'port_seen == 3', '-123'),
        'serial_putchar': ("'X'", "port_seen == 4 && port_char == 'X'", None),
        'serial_trygetchar': ('', 'port_seen == 5', '-125'),
        'serial_is_initialized': ('', 'port_seen == 6', '126'),
        'serial_init_vfast': ('0x87654321u', 'port_seen == 7 && port_baud == 0x87654321ul', '-127'),
        'serial_get_status': ('&mode, &baud, &fifo', 'port_seen == 8 && mode == 11 && baud == 12 && fifo == 13', '-128'),
        'serial_diag': ('&serial_diag', 'port_seen == 9 && serial_diag.oe == 21 && serial_diag.fe == 22 && serial_diag.pe == 23 && serial_diag.overflow == 24', '0'),
    }
    for name, (call_args, check, result) in port_calls.items():
        slot = next(i for i, e in enumerate(spec['api']) if e['name'] == name)
        entry = spec['api'][slot]
        if name != 'serial_diag':
            assert entry['target'].startswith('port_'), name
        match = re.search(r'^(?:int|void) __cdecl wrap_' + name +
                          r'\([^\n]*\)\n\{.*?^\}', wrappers, re.M | re.S)
        assert match, name
        port_selected.append(match[0])
        call = f'wrap_{name}({call_args})'
        port_checks.append((f'CHECK({call} == {result});' if result else call + ';') +
                           f'CHECK({check} && hit == {slot});')
    sys_source = (ROOT / 'kapi/kapi_sys.c').read_text()
    diag_body = re.search(r'^int kapi_serial_diag\([^\n]*\)\n\{.*?^\}', sys_source, re.M | re.S)
    assert diag_body
    port_selected.insert(0, diag_body[0])
    port_checks += ['CHECK(kapi_serial_diag(0) == OS32_ERR_INVAL);',
                    "CHECK(port_putchar_kapi('Z') == -124 && port_char == 'Z');",
                    'port_diag_get(0, 0, 0, 0); CHECK(port_seen == 9);',
                    'CHECK(port_get_status(0, 0, 0) == -128);']
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
    rtc = (ROOT / 'drivers/char/rtc.c').read_text()
    adapter = rtc[rtc.index('static void pc98_read_date('):]
    if mutation == 'clock_pivot':
        assert 'time.year < RTC_YEAR_PIVOT' in adapter
        adapter = adapter.replace('time.year < RTC_YEAR_PIVOT', 'time.year <= RTC_YEAR_PIVOT')
    clock_slot = next(i for i, e in enumerate(spec['api']) if e['name'] == 'rtc_read')
    assert spec['api'][clock_slot]['body'] == 'clock_read_legacy((RTC_Time *)rtc_time);'
    clock_wrapper = re.search(r'^void __cdecl wrap_rtc_read\([^\n]*\)\n\{.*?^\}', wrappers, re.M | re.S)
    assert clock_wrapper
    sys_time = re.search(r'^os_time_t sys_time\(void\)\n\{.*?^\}',
                         (ROOT / 'kernel/sys.c').read_text(), re.M | re.S)
    diskio = (ROOT / 'fs/fat/fatfs/diskio.c').read_text()
    fattime = re.search(r'^DWORD get_fattime\(void\)\n\{.*?^\}', diskio, re.M | re.S)
    assert sys_time and fattime
    constants = '\n'.join(re.findall(r'^#define DOS_.*$', diskio, re.M))
    with tempfile.TemporaryDirectory(prefix='d2-input-') as directory:
        work = Path(directory)
        fixture = (ROOT / 'tools/tests/d2_services_host.c').read_text()
        fixture = fixture.replace('#include "../../kernel/input_service.c"', service)
        fixture = fixture.replace('#include "../../platform/pc98/d2_bind.c"', bind)
        fixture = fixture.replace('#include "../../drivers/input/kbd.h"', '#include "' + str(ROOT / 'drivers/input/kbd.h') + '"')
        fixture = fixture.replace('#include "../../drivers/input/mouse.h"', '#include "' + str(ROOT / 'drivers/input/mouse.h') + '"')
        fixture = fixture.replace('#include \"pointer_fixture_host.h\"', '#include \"' + str(ROOT / 'tools/tests/pointer_fixture_host.h') + '\"')
        fixture = fixture.replace('#include "port_fixture_host.h"', '#include "' + str(ROOT / 'tools/tests/port_fixture_host.h') + '"')
        fixture = fixture.replace('#include "../../drivers/char/serial.h"', '#include "' + str(ROOT / 'drivers/char/serial.h') + '"')
        fixture = fixture.replace('#include "clock_fixture_host.h"', '#include "' + str(ROOT / 'tools/tests/clock_fixture_host.h') + '"')
        fixture = fixture.replace('#include "../../drivers/char/rtc.h"', '#include "' + str(ROOT / 'drivers/char/rtc.h') + '"')
        (work / 'clock_adapter.inc').write_text(adapter)
        (work / 'clock_consumers.inc').write_text('typedef u32 DWORD;\n' + constants + '\n' + sys_time[0] + '\n' + fattime[0])
        (work / 'clock_wrapper.inc').write_text(f'#define CLOCK_SLOT {clock_slot}\n' + clock_wrapper[0])
        (work / 'fixture.c').write_text(fixture)
        helper = wrappers[wrappers.index('#define KAPI_OUT_LEN('):wrappers.index('static int kapi_in_range(')]
        (work / 'input_wrappers.inc').write_text(helper + '\n'.join(selected))
        (work / 'input_wrapper_checks.inc').write_text('\n'.join(checks))
        shift = re.search(r'^u8 fep_kbd_shift_state\(void\).*$',
                          (ROOT / 'kernel/ime/fep_module.c').read_text(), re.M)
        assert shift
        (work / 'input_shift.inc').write_text(shift[0])
        (work / 'port_wrappers.inc').write_text('\n'.join(port_selected))
        (work / 'port_wrapper_checks.inc').write_text('\n'.join(port_checks))
        (work / 'pointer_wrappers.inc').write_text('\n'.join(pointer_selected))
        (work / 'pointer_wrapper_checks.inc').write_text('\n'.join(pointer_checks))
        exe = work / 'test'
        includes = include_flags(ROOT)
        subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-Wno-unused-const-variable',
                        '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                        '-nostdlib', '-static', '-no-pie', *includes, '-I' + str(work),
                        str(work / 'fixture.c'), '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner=runner, timeout=30)
        assert result.returncode == (1 if mutation else 0), result.returncode
    print('PASS D2 A/B/C input/pointer/port/emergency/clock' + (' RED ' + mutation if mutation else ' (28 public wrappers)'))


def contracts():
    boot = (ROOT / 'init/kernel.c').read_text()
    assert re.search(r'kstack_hw_init\(\);\s*pc98_boot_bind\(&boot, boot_drive\);', boot)
    platform = (ROOT / 'platform/pc98/boot.c').read_text()
    assert '    pc98_d2_bind();' in platform
    assert boot.index('pc98_boot_bind(&boot, boot_drive);') < boot.index('boot_platform_run(BOOT_INPUT, &boot);')
    for name in ('fs/vfs_fd.c', 'exec/exec.c', 'kernel/gui.c', 'kernel/ime/fep_module.c'):
        source = (ROOT / name).read_text()
        assert '#include "input_service.h"' in source
        assert not re.search(r'#include.*drivers/(kbd|mouse)\.h', source)
        code = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
        assert not re.search(r'\bkbd_(?:getchar|getkey|set_gui_mode|discard_stop)\s*\(', code)
    for name, header in (('kernel/ime/ime.c', '../../drivers/input/kbd.h'),
                         ('kdebug/kselftest.c', '../drivers/input/kbd.h')):
        assert '#include "' + header + '"' in (ROOT / name).read_text()
    assert '#include "../../drivers/input/mouse.h"' in platform
    assert 'drivers/input/mouse.h' not in boot and 'drivers/input/kbd.h' not in boot
    assert '#include "../../include/pointer_service.h"' in (ROOT / 'drivers/input/mouse.h').read_text()
    assert 'drivers/' not in (ROOT / 'include/pointer_service.h').read_text()
    common = (ROOT / 'include/kbd.h').read_text()
    assert 'drivers/' not in common and 'kbd_init' not in common and 'KBD_DATA' not in common
    assert '#include "../../include/kbd.h"' in (ROOT / 'drivers/input/kbd.h').read_text()
    # The common header must resolve without the drivers include search path.
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-Werror', '-fsyntax-only', '-x', 'c',
                    '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'sdk/include/os32'), '-'],
                   input='#include "input_service.h"\n#include "pointer_service.h"\n#include "port_service.h"\n#include "console_output.h"\n#include "clock_service.h"\n', text=True, check=True)
    with tempfile.TemporaryDirectory(prefix='d2-symbols-') as directory:
        obj = Path(directory) / 'input_service.o'
        subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie',
                        *include_flags(ROOT),
                        '-c', str(ROOT / 'kernel/input_service.c'), '-o', str(obj)], check=True)
        symbols = subprocess.check_output(['nm', '-u', str(obj)], text=True)
        assert not symbols.strip(), symbols
    bind = (ROOT / 'platform/pc98/d2_bind.c').read_text()
    for registration in ('port_register(&pc98_port);', 'con_output_register(&pc98_console_output);'):
        assert registration in bind
    assert 'const EmergencyOps emergency_output' in bind
    for name in ('kernel/console/console.c', 'kernel/ver_collect.c', 'kapi/kapi_sys.c',
                 'exec/exec.c', 'arch/x86/kernel/isr_handlers.c', 'arch/x86/kernel/double_fault.c'):
        source = (ROOT / name).read_text()
        assert not re.search(r'#include[^\n]*["/]serial\.h"', source), name
    for header in ('port_service.h', 'console_output.h'):
        assert 'drivers/' not in (ROOT / 'include' / header).read_text()
    with tempfile.TemporaryDirectory(prefix='d2-port-symbols-') as directory:
        for source in ('port_service', 'serial_diag', 'clock_service'):
            obj = Path(directory) / (source + '.o')
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie',
                            *include_flags(ROOT), '-c',
                            str(ROOT / ('kernel/time/clock_service.c' if source == 'clock_service' else 'kernel/' + source + '.c')), '-o', str(obj)], check=True)
            symbols = subprocess.check_output(['nm', '-u', str(obj)], text=True)
            assert symbols.split() == (['U', 'emergency_output'] if source == 'serial_diag' else []), symbols
    assert 'clock_register(&pc98_clock);' in bind
    for name in ('kernel/sys.c', 'fs/fat/fatfs/diskio.c'):
        source = (ROOT / name).read_text()
        assert '#include "clock_service.h"' in source
        assert 'RTC_YEAR_' not in source and 'rtc_read(' not in source
    assert 'drivers/' not in (ROOT / 'include/clock_service.h').read_text()
    print('PASS D2 A/B/C dependency, symbols and boot wiring contracts')



def generation_contract():
    # Recreate the old RTC call in a temporary tree; public generated bytes stay equal.
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    with tempfile.TemporaryDirectory(prefix='d2-clock-gen-') as directory:
        work = Path(directory)
        for path in ('sdk/include/os32', 'kapi', 'exec', 'sdk/rust/os32api/src'):
            (work / path).mkdir(parents=True)
        source = work / 'sdk/kapi.json'
        source.write_text(json.dumps(spec))
        def generate():
            subprocess.run(['python3', str(ROOT / 'sdk/gen_kapi.py')], cwd=work,
                           check=True, stdout=subprocess.DEVNULL)
            return {str(p.relative_to(work)): p.read_bytes() for p in work.rglob('*')
                    if p.is_file() and p != source}
        first = generate()
        assert first == generate(), 'generation must be deterministic'
        rtc = next(e for e in spec['api'] if e['name'] == 'rtc_read')
        rtc['body'] = 'rtc_read((RTC_Time *)rtc_time);'
        spec['includes'] = ['rtc.h' if h == 'clock_service.h' else h for h in spec['includes']]
        source.write_text(json.dumps(spec))
        old = generate()
        for name, data in first.items():
            if name != 'kapi/kapi_generated.c':
                assert old[name] == data, name
        assert b'clock_read_legacy((RTC_Time *)rtc_time);' in first['kapi/kapi_generated.c']
        assert b'rtc_read((RTC_Time *)rtc_time);' in old['kapi/kapi_generated.c']
    print('PASS D2 clock generator target-only change and repeat generation')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    contracts()
    generation_contract()
    run(args.runner)
    if args.mutate:
        for mutation in ('unbound', 'shift', 'target', 'pointer_bounds', 'clock_pivot'):
            run(args.runner, mutation)
