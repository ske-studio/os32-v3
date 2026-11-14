"""Capture keyboard deadlines and serial isolation using the real driver."""
CHECK = dict(name='remote-x8a', tier=1, needs=[], budget=4.0,
             covers=['drivers/input/kbd.c', 'drivers/input/kbd_fgpoll.inc',
                     'include/**', 'drivers/input/*.h', 'drivers/char/serial.h',
                     'drivers/video/geforce/geforce_module.h', 'kernel/idle_work.h',
                     'kernel/module/*.h', 'platform/pc98/legacy/v86_bios.h',
                     'tools/tests/test_remote_x8a.py', 'tools/tests/test_kbd_fgpoll.py',
                     'tools/tests/kbd_fgpoll_host.c', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'tools/tests/host_arch/**', 'build/config.mk'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import test_kbd_fgpoll as base
import host32

ROOT = Path(__file__).resolve().parents[2]
CASE = '''
    fresh(); capture = 1; rshell_active = 1; serial_byte = 's'; wait_mode = 2;
    tick_count = U32_MAX - PIT_HZ;
    CHECK(kbd_trygetchar() == -1 && serial_byte == 's', "capture trychar serial");
    CHECK(kbd_trygetkey() == -1 && serial_byte == 's', "capture trykey serial");
    polls = 0; rshell_active = 0;
    CHECK(kbd_getchar_wait(0) == '\\r' && halts == 2 && serial_byte == 's', "capture char deadline");
    fresh(); capture = 1; rshell_active = 1; serial_byte = 's'; wait_mode = 2;
    CHECK(kbd_getchar_wait(0) == '\\r' && serial_byte == 's', "capture wait serial");
    fresh(); capture = 1; wait_mode = 2; tick_count = U32_MAX - PIT_HZ;
    CHECK(kbd_getkey_wait(0) == 0x1b && halts == 2, "capture key deadline");
'''
MUTATIONS = [
    ('char deadline', 'rshell_active || con_sink_capture_active()', 'rshell_active'),
    ('key deadline', 'if (capture && (u32)(tick_count - start_tick)', 'if (0 && capture && (u32)(tick_count - start_tick)'),
    ('trychar serial', 'if (rshell_active && !con_sink_capture_active()) {', 'if (rshell_active) {'),
    ('wait serial', '            if (rshell_active && !con_sink_capture_active()) {', '            if (rshell_active) {'),
    ('trykey serial', '    if (rshell_active && !con_sink_capture_active()) {\n        int sch;\n        sch = serial_trygetchar();\n        if (sch >= 0) return sch; /*', '    if (rshell_active) {\n        int sch;\n        sch = serial_trygetchar();\n        if (sch >= 0) return sch; /*'),
]

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true'); args = parser.parse_args()
    source = (ROOT / base.KBD).read_text()
    fixture = (ROOT / 'tools/tests/kbd_fgpoll_host.c').read_text()
    fixture = fixture.replace('int con_sink_capture_active(void) { return 0; }', 'static int capture;\nint con_sink_capture_active(void) { return capture; }')
    fixture = fixture.replace('pending = rshell_active = kbd_gui_mode = wait_mode = 0;', 'pending = rshell_active = kbd_gui_mode = wait_mode = capture = 0;')
    fixture = fixture.replace('    say("GREEN keyboard', CASE + '\n    say("GREEN keyboard')
    with tempfile.TemporaryDirectory(prefix='remote-x8a-') as d:
        tmp = Path(d); exe = tmp / 'host'; (tmp / 'host.c').write_text(fixture)
        adapter = (ROOT / base.ADAPTER).read_text()
        import re
        def run(src):
            def relocate(m):
                if m[1] == 'kbd_fgpoll.inc': return '#include "' + str(tmp / 'kbd_fgpoll.inc') + '"'
                path = ROOT / 'drivers/input' / m[1]
                return '#include "' + str(path.resolve()) + '"' if path.is_file() else m[0]
            (tmp / 'kbd_source.c').write_text(re.sub(r'^#include "([^"]+)"', relocate, src, flags=re.M))
            (tmp / 'kbd_fgpoll.inc').write_text(adapter)
            cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__', '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-I'+str(tmp), '-I'+str(ROOT / 'tools/tests/host_arch'), *base.include_flags(ROOT), str(tmp / 'host.c'), '-o', str(exe)]
            subprocess.run(cmd, cwd=ROOT, check=True)
            return host32.run([str(exe)], runner='qemu', capture_output=True, text=True, timeout=10)
        result = run(source); assert result.returncode == 0, result.stdout; print(result.stdout, end='')
        if args.mutate:
            for name, old, new in MUTATIONS:
                if name == 'trychar serial':
                    old = '    ' + old + '\n        int sch;\n        sch = serial_trygetchar();\n        if (sch >= 0) return sch;\n'
                    new = '    ' + new + '\n        int sch;\n        sch = serial_trygetchar();\n        if (sch >= 0) return sch;\n'
                assert source.count(old) == 1, name
                result = run(source.replace(old, new, 1))
                assert result.returncode == 1 and 'FAIL ' in result.stdout, (name, result.stdout)
                print('MUTATE runtime RED', name, result.stdout.strip())

def run(ctx):
    return subprocess.call(['python3', '-B', __file__] + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)

if __name__ == '__main__': main()
