"""F0e: execute the real common syscall tail after caller restoration.

Resident scheduling is verified by test_geforce_fgpoll; this boundary test
checks the hot-path gate and delivery of a poll's abort before ring3 return.
Full service/module composition remains U6's acceptance test.
"""
CHECK = dict(name='exec-syscall-fgpoll-host', tier=1, budget=2.8,
             covers=['arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h', 'tools/tests/host_exec_context.h', 'tools/tests/test_exec_syscall_fgpoll.py',
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
#include "host_exec_context.h"
#include <stdbool.h>
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(c) do { if (!(c)) finish(1); } while (0)
static int caller, ring3_in_syscall, checks, polls, aborts, parks;
static bool due, stop_pending;
static struct arch_syscall *g_cur_frame, *saved_frame;
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
#include "exec_gfx_poll.h"
static void ring3_abort_check(void) {
    CHECK(checks==1 && polls==(int)due);
    if (stop_pending) CHECK(aborts==1);
}
static void exec_park_stop(struct arch_syscall *frame) { CHECK(arch_syscall_slot(frame)==1234); parks++; }
static void syscall_tail(u32 *frame) {
    int prev_caller=0, prev_in_syscall=7;
    struct arch_syscall *prev_frame=saved_frame;
    struct arch_syscall call = { frame };
    u32 result = 1234;
/* PRODUCT */
void _start(void) {
    u32 frame[13]={0}, parent[13]={0};
    struct arch_syscall parent_call = { parent }, child_call = { frame };
    frame[7]=1234; saved_frame=&parent_call;
    for (int mode=0; mode<3; mode++) {
        caller=1; ring3_in_syscall=1; g_cur_frame=&child_call;
        due=mode!=0; stop_pending=mode==2;
        checks=polls=aborts=parks=0;
        syscall_tail(frame);
        CHECK(checks==1 && polls==(int)due && parks==1);
        CHECK(!caller && ring3_in_syscall==7 && g_cur_frame==&parent_call);
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
                    '-no-pie', '-ffunction-sections', '-Wl,--gc-sections', '-I'+str(ROOT/'tools/tests'), *include_flags(ROOT), str(fixture), '-o', str(exe)],
                   check=True)
    result = host32.run([str(exe)], capture_output=True, timeout=10)
    assert result.returncode == (1 if mutant else 0), (label, result)
    print(('MUTATE runtime RED: ' if mutant else 'GREEN: ') + label, flush=True)


def run_entry(folder, source, mutant=False):
    start = source.index('    ring3_gui_pump();')
    end = source.index('    arch_kapi_source(', start)
    entry = source[start:end]
    fixture = folder / 'entry.c'
    fixture.write_text(r"""
static int pumps, work;
static void ring3_gui_pump(void) { pumps++; }
static __attribute__((unused)) void idle_work_run(void) { work++; }
void _start(void) {
""" + entry + r"""
    int rc = !(pumps == 1 && work == 1);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
""")
    exe = folder / 'entry'
    subprocess.run(['gcc', '-m32', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    '-nostdlib', '-static', '-no-pie', str(fixture), '-o', str(exe)], check=True)
    rc = host32.run([str(exe)]).returncode
    assert rc == (1 if mutant else 0), rc
    print(('MUTATE runtime RED: ' if mutant else 'GREEN: ') + 'syscall-idle', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'exec/exec_syscall.inc').read_text()
    with tempfile.TemporaryDirectory(prefix='syscall-fgpoll-') as td:
        folder = Path(td)
        run_case(folder, source, 'syscall-tail')
        run_entry(folder, source)
        if args.mutate:
            assert source.count('    idle_work_run();') == 1
            run_entry(folder, source.replace('    idle_work_run();', ''), True)
            old = 'if (exec_gfx_poll_due(tick_count)) exec_gfx_poll();'
            assert source.count(old) == 1
            run_case(folder, source.replace(old,
                     'if (exec_gfx_poll_due(tick_count) && 0) exec_gfx_poll();'),
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
