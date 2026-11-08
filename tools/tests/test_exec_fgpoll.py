"""F1: compile the real poll/yield entries; observe dispatch before park/halt.

The scheduler and resident poll are boundary stubs. Full thermal/module
composition belongs to U6; no sensor timing or IRQ execution is modeled here.
"""
CHECK = dict(name='exec-fgpoll-host', tier=1, budget=2.7,
             covers=['tools/tests/test_exec_fgpoll.py',
                     'exec/exec_schedule.inc', 'exec/appslot.h', 'exec/exec.h',
                     'tools/tests/kernel_includes.py', 'tools/tests/host32.py',
                     'build/config.mk', 'include/**', 'arch/x86/**',
                     'sdk/include/os32/**', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py',
                     'mm/paging.h', 'fs/vfs_stdin.h', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h'])

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'exec/exec_schedule.inc'


def function(source, name):
    start = re.search(r'^(?:int|i32) ' + name + r'\([^;{}]*\)\n\{', source, re.M)
    assert start, name
    return source[start.start():source.index('\n}', start.end()) + 2]


HOST = r'''
#include "appslot.h"
#include "host_exec_context.h"
#include "exec.h"
static void die(int rc) __attribute__((noreturn));
static void die(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
static void say(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(c) do { if (!(c)) { say("FAIL foreground before park/halt\n"); die(1); } } while (0)
static AppSlot app, other, *g_cur_app;
static u32 frame[APP_FRAME_WORDS];
static struct arch_syscall call = { frame }, *g_cur_frame;
static int font_guard_active, gui, reject, bad_slot, polls, halts, transfers;
volatile u32 ring3_park_reject_count;
void geforce_foreground_poll(void) { polls++; }
static void _halt(void) { CHECK(polls == 1); halts++; }
int con_sink_is_enabled(void) { CHECK(polls == 1); return gui; }
int appslot_cur(void) { CHECK(polls == 1); return 2; }
int appslot_park_poll_check(u32 tick) { CHECK(polls == 1 && tick == 7); return reject; }
int appslot_park_yield_check(void) { CHECK(polls == 1); return reject; }
AppSlot *appslot_get(int id) {
    CHECK(polls == 1 && id == 2);
    return bad_slot == 1 ? 0 : bad_slot == 2 ? &other : &app;
}
void appslot_park_poll_commit(void) {}
void appslot_park_yield_commit(void) {}
static void exec_park_transfer(int id, AppSlot *a, const struct arch_syscall *f, void (*commit)(void)) {
    CHECK(polls == 1 && !halts && id == 2 && a == &app && f == &call);
    CHECK(commit == appslot_park_poll_commit || commit == appslot_park_yield_commit);
    transfers++;
}
/* PRODUCT */
static void reset(void) {
    g_cur_app = &app; app.cpl3 = 1; g_cur_frame = &call;
    gui = 1; font_guard_active = reject = bad_slot = 0;
    polls = halts = transfers = 0; ring3_park_reject_count = 0;
}
void _start(void) {
    /* Guarded font callbacks may neither poll nor yield. */
    reset(); font_guard_active = 1;
    CHECK(exec_park_poll(7) == 0 && exec_sys_yield() == OS32_ERR_BUSY);
    CHECK(!polls && !halts && !transfers);
    /* poll only accepts a ring3 syscall context, before tick throttling. */
    for (int mode = 0; mode < 3; mode++) {
        reset();
        if (mode == 0) g_cur_app = 0;
        if (mode == 1) app.cpl3 = 0;
        if (mode == 2) g_cur_frame = 0;
        CHECK(exec_park_poll(7) == 0 && !polls && !halts && !transfers);
    }
    reset(); reject = -1;
    CHECK(exec_park_poll(7) == 0 && polls == 1 && !transfers);
    polls = 0;
    CHECK(exec_park_poll(7) == 0 && polls == 1 && !transfers);
    for (int mode = 0; mode < 3; mode++) {
        reset(); bad_slot = mode;
        CHECK(exec_park_poll(7) == 0 && polls == 1 && !halts);
        CHECK(transfers == (mode == 0));
        CHECK(ring3_park_reject_count == (u32)(mode != 0));
    }
    /* Each early yield halt, including CUI and CPL0, dispatches once. */
    for (int mode = 0; mode < 7; mode++) {
        reset();
        if (mode == 0) { gui = 0; g_cur_app = 0; g_cur_frame = 0; }
        if (mode == 1) g_cur_app = 0;
        if (mode == 2) app.cpl3 = 0;
        if (mode == 3) g_cur_frame = 0;
        if (mode == 4) reject = -1;
        if (mode == 5) bad_slot = 1;
        if (mode == 6) bad_slot = 2;
        CHECK(exec_sys_yield() == 0 && polls == 1 && halts == 1 && !transfers);
        CHECK(ring3_park_reject_count == (u32)(mode >= 5));
    }
    reset();
    CHECK(exec_sys_yield() == 0 && polls == 1 && !halts && transfers == 1);
    say("GREEN foreground before park/halt\n");
    die(0);
}
'''


def run_case(folder, source, name, mutant=False):
    parts = '\n'.join(function(source, name) for name in
                      ('exec_park_poll', 'exec_sys_yield'))
    fixture = folder / 'host.c'
    fixture.write_text(HOST.replace('/* PRODUCT */', parts))
    exe = folder / 'host'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-ffunction-sections',
                    '-fdata-sections', '-Wl,--gc-sections', '-ffreestanding', '-fno-builtin',
                    '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static',
                    '-no-pie', '-I'+str(ROOT/'tools/tests'), *include_flags(ROOT), str(fixture), '-o', str(exe)],
                   check=True)
    result = host32.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == (1 if mutant else 0), (name, result)
    assert ('FAIL' if mutant else 'GREEN') + ' foreground before park/halt' in result.stdout
    print(('MUTATE runtime RED: ' if mutant else 'GREEN: ') + name, flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory(prefix='exec-fgpoll-') as td:
        folder = Path(td)
        run_case(folder, source, 'poll/yield')
        if args.mutate:
            for name in ('exec_park_poll', 'exec_sys_yield'):
                original = function(source, name)
                anchor = '    geforce_foreground_poll();\n'
                assert original.count(anchor) == 1, name
                assert source.count(original) == 1, name
                changed = source.replace(original, original.replace(anchor, '', 1), 1)
                run_case(folder, changed, name, mutant=True)
                run_case(folder, source, name + '-restored')


def run(ctx) -> int:
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = [sys.executable, '-B', 'tools/tests/test_exec_fgpoll.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
