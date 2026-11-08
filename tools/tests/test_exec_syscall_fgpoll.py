"""F0e: execute the real common syscall tail after caller restoration.

Resident scheduling is verified by test_geforce_fgpoll; this boundary test
checks the hot-path gate and delivery of a poll's abort before ring3 return.
Full service/module composition remains U6's acceptance test.
"""
CHECK = dict(name='exec-syscall-fgpoll-host', tier=1, budget=2.8,
             covers=['tools/tests/test_exec_syscall_fgpoll.py',
                     'exec/exec_syscall.inc', 'drivers/video/geforce/geforce_module.h',
                     'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'build/config.mk',
                     'include/**'])

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
HOST = r'''
#include "types.h"
#include <stdbool.h>
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(c) do { if (!(c)) finish(1); } while (0)
static int caller, ring3_in_syscall, checks, polls, aborts, parks;
static bool due, stop_pending;
static u32 *g_cur_frame, *saved_frame;
static u32 tick_count=100;
static void caller_access_leave(const int *prev) { caller=*prev; }
static void exec_cui_display_abort(void) { CHECK(!caller); }
bool geforce_fg_due(u32 now) {
    CHECK(!caller && ring3_in_syscall==7 && g_cur_frame==saved_frame);
    CHECK(now==tick_count); checks++; return due;
}
void geforce_foreground_poll(void) {
    CHECK(checks==1 && !caller && ring3_in_syscall==7);
    polls++;
    if (stop_pending) aborts++;
}
static void ring3_abort_check(void) {
    CHECK(checks==1 && polls==(int)due);
    if (stop_pending) CHECK(aborts==1);
}
static void exec_park_stop(u32 *frame) { CHECK(frame[7]==1234); parks++; }
static void syscall_tail(u32 *frame) {
    int prev_caller=0, prev_in_syscall=7;
    u32 *prev_frame=saved_frame;
/* PRODUCT */
void _start(void) {
    u32 frame[13]={0}, parent[13]={0};
    frame[7]=1234; saved_frame=parent;
    for (int mode=0; mode<3; mode++) {
        caller=1; ring3_in_syscall=1; g_cur_frame=frame;
        due=mode!=0; stop_pending=mode==2;
        checks=polls=aborts=parks=0;
        syscall_tail(frame);
        CHECK(checks==1 && polls==(int)due && parks==1);
        CHECK(!caller && ring3_in_syscall==7 && g_cur_frame==parent);
    }
    finish(0);
}
'''


def run_case(folder, source, label, mutant=False):
    start = source.index('syscall_complete:') + len('syscall_complete:')
    tail = source[start:]
    fixture = folder / 'host.c'
    fixture.write_text(HOST.replace('/* PRODUCT */', tail))
    exe = folder / 'host'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-ffreestanding',
                    '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static',
                    '-no-pie', *include_flags(ROOT), str(fixture), '-o', str(exe)],
                   check=True)
    result = host32.run([str(exe)], capture_output=True, timeout=10)
    assert result.returncode == (1 if mutant else 0), (label, result)
    print(('MUTATE runtime RED: ' if mutant else 'GREEN: ') + label, flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'exec/exec_syscall.inc').read_text()
    with tempfile.TemporaryDirectory(prefix='syscall-fgpoll-') as td:
        folder = Path(td)
        run_case(folder, source, 'syscall-tail')
        if args.mutate:
            old = 'if (geforce_fg_due(tick_count)) geforce_foreground_poll();'
            assert source.count(old) == 1
            run_case(folder, source.replace(old,
                     'if (geforce_fg_due(tick_count) && 0) geforce_foreground_poll();'),
                     'syscall-poll', True)
            run_case(folder, source, 'syscall-poll-restored')


def run(ctx) -> int:
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    command = [sys.executable, '-B', 'tools/tests/test_exec_syscall_fgpoll.py']
    if ctx.mutate:
        command.append('--mutate')
    return subprocess.call(command, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
