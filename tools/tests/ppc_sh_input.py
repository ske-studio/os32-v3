#!/usr/bin/env python3
"""Exercise the actual PowerMac terminal input provider and decoder."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import host32

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-sh-input', tier=1, budget=60, needs=[],
             covers=['platform/powermac/input_usb.h', 'tools/tests/host32.py', 'platform/powermac/terminal_input.c', 'platform/powermac/input_boot.c', 'platform/powermac/input_boot.h',
                     'platform/powermac/input_decode.c'],
             inputs=['tools/tests/ppc_sh_input.py', 'platform/powermac/terminal_input.c',
                     'platform/powermac/lan_boot.h', 'kernel/cli/resident_cli.h',
                     'include/input_service.h', 'include/clock_source.h', 'include/types.h'])
HARNESS = r'''
static void exit(int code) __attribute__((noreturn));
static void exit(int code) { __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); __builtin_unreachable(); }
static void message(const char *s) { unsigned n=0; while(s[n]) n++; __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(s), "d"(n) : "memory"); }
#define fprintf(fd, fmt, ...) message(fmt)

#include "input_boot.h"
#include "input_service.h"
#include "lan_boot.h"
int pm_input_usb_take(void) { return -1; }
u32 pm_input_usb_dropped(void) { return 0; }
volatile u32 tick_count;
static u32 clock_us, clock_step;
int platform_clock_read_us(u32 *lo, u32 *hi) { *lo = clock_us; *hi = 0; clock_us += clock_step; return 0; }
static const InputOps *ops;
void input_register(const InputOps *p) { ops = p; }
u64 pm_terminal_tb(void) { return 0; }
void pm_lan_poll(u64 now) { (void)now; }
static void feed(const char *s, u32 n) { pm_terminal_feed((const u8 *)s, n); }
static void check(int ok, int code, const char *msg) { if (!ok) { message(msg); message("\n"); exit(code); } }
static int test_main(void) {
    pm_input_boot(); pm_terminal_open();
    check(ops->trygetkey() == 27, 12, "cancel-fail");
    feed("\n\r\n\177\003", 5);
    check(ops->getkey() == 13 && ops->getkey() == 13 && ops->getkey() == 8 && ops->getkey() == 27, 12, "cancel-fail");
    pm_terminal_line_lost();
    check(ops->trygetkey() == 27, 12, "cancel-fail");
    pm_terminal_open(); check(ops->trygetkey() == 27, 12, "cancel-fail");
    fprintf(stderr, "cancel-ok\n");
    feed("\033[A", 3);
    check(ops->peekkey() == 0x3A00 && ops->peekkey() == 0x3A00 && ops->getkey() == 0x3A00 && !ops->has_key(), 11, "arrows-fail");
    feed("\033", 1); check(ops->trygetkey() == -1, 11, "arrows-fail");
    feed("[", 1); check(ops->trygetkey() == -1, 11, "arrows-fail");
    feed("B", 1); check(ops->getkey() == 0x3D00, 11, "arrows-fail");
    fprintf(stderr, "arrows-ok\n");
    feed("\033", 1); check(ops->trygetkey() == -1, 13, "timeout-fail");
    clock_step = 10000; check(ops->getkey() == 27 && tick_count == 0, 13, "timeout-fail");
    clock_step = 0; fprintf(stderr, "timeout-ok\n");
    feed("\033", 1); check(ops->trygetkey() == -1, 14, "late-fail");
    clock_us += 50000; feed("[A", 2);
    check(ops->getkey() == 27 && ops->getkey() == '[' && ops->getkey() == 'A', 14, "late-fail");
    fprintf(stderr, "late-ok\n"); return 0;
}
void _start(void) { exit(test_main()); }
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'platform/powermac/input_decode.c').read_text()
    mutations = [('arrows', "byte == 'A' ? 0x3A00 : 0x3D00", "byte == 'A' ? 0x3D00 : 0x3A00", 11, 'cancel-ok\narrows-fail\n'),
                 ('cancel', '        d->state = d->after_cr = 0;\n        return ESC;', '        d->state = d->after_cr = 0;\n        return -1;', 12, 'cancel-fail\n')]
    mutations.append(('late', '    if (d->state && (u32)(now - d->started) >= ESC_TIMEOUT_US)', '    if (byte < 0 && d->state && (u32)(now - d->started) >= ESC_TIMEOUT_US)', 14, 'cancel-ok\narrows-ok\ntimeout-ok\nlate-fail\n'))
    cases = [('normal', source, 0, 'cancel-ok\narrows-ok\ntimeout-ok\nlate-ok\n')]
    for name, old, new, code, marker in mutations:
        if source.count(old) != 1:
            raise RuntimeError(f'{name}: mutation anchor is not unique')
        if args.mutate:
            cases.append((name, source.replace(old, new), code, marker))
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        d = Path(tmp)
        (d / 'harness.c').write_text(HARNESS)
        for name, code, expected, marker in cases:
            (d / 'decode.c').write_text(code)
            subprocess.run(['gcc', '-m32', '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-DPM_TERMINAL_TEST',
                            '-I.', '-Iinclude', '-Isdk/include/os32', '-Idrivers/input', '-Iplatform/powermac',
                            str(d / 'harness.c'), str(d / 'decode.c'), 'platform/powermac/input_boot.c',
                            'platform/powermac/terminal_input.c', '-o', str(d / 'test')], cwd=ROOT, check=True)
            result = host32.run([str(d / 'test')], runner='qemu', capture_output=True, text=True, timeout=10)
            if result.returncode != expected or result.stderr != marker:
                raise RuntimeError(f'{name}: exit {result.returncode}: {result.stderr!r}')
            print(('OK ' if name == 'normal' else 'RED ') + name, flush=True)


if __name__ == '__main__':
    main()
