#!/usr/bin/env python3
"""PPC_P3_DESIGN v2 §4 / appendix A-3: USB priority, exits and cleanup."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import sys
import host32

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='rshell-app', tier=1, needs=[], budget=20,
    covers=['userland/shell/rshell_lan.c', 'userland/shell/shell.h',
            'userland/shell/serial_watchdog.c',
            'userland/shell/serial_watchdog.h', 'userland/ppc/sh/link_profile.h',
            'userland/ppc/sh/profile.h', 'tools/tests/test_rshell_app.py',
            'tools/tests/host32.py', 'sdk/include/os32/*.h', 'include/types.h'],
    basis=['PPC_P3_DESIGN v2 §4, appendix A-3', 'PACK PM flag decision'])

HARNESS = r'''
#include "shell.h"
#include "serial_watchdog.h"
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static KernelAPI api;
KernelAPI *g_api = &api;
int sh_exit_flag, sh_exit_code;
static int mode, step, polls, halts, yields, resets, on, off, commands, failed, refused, status;
static const char *input;
static const ShellCmd *registered;
static void die(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); for (;;) {} }
static int local(void) {
    if (++step > 400) die(2);
    int c = *input;
    if (c) { input++; return c == '_' ? -1 : c; }
    return -1;
}
static int poll(void) {
    polls++;
    if (mode == 0) failed = 1; /* all gaps are inside a USB half-line */
    return mode == 1 ? RSH_LAN_EXIT : OS32_ERR_AGAIN;
}
static void reset(void) { resets++; }
static void active(int n) { if (n) on++; else off++; }
static void halt(void) { halts++; }
static i32 yield(void) { yields++; return 0; }
int execute_command(const char *s) {
    commands++;
    if (s[0] == 'e') { sh_exit_flag = 1; sh_exit_code = 37; }
    else if (s[0] != 'x' || s[1] != 0) failed = 1;
    return 0;
}
void sh_refuse(const char *s, int n) { (void)s; (void)n; if (mode != 3) failed = 1; refused++; }
void sh_status_set(int s) { status = s; }
int sh_refused_take(void) { return 0; }
void shell_register_cmds(const ShellCmd *c) { registered = c; }
void rsh_lan_trace_show(void) {}
#define rsh_lan_poll poll
#define rsh_lan_reset reset
#include "APP"
#undef rsh_lan_poll
#undef rsh_lan_reset
static int run(void) {
    rsh_lan_app_init();
    if (!registered || registered[0].name[0] != 'r' || registered[1].name) return 1;
    if (rsh_lan_execute("x") || commands != 1 || failed) return 1;
    /* Review P2-1: a LAN `exit N` leaves no pending exit for later lines. */
    if (rsh_lan_execute("e") || sh_exit_flag) return 1;
    if (rsh_lan_handoff("gui") != OS32_ERR_NOSYS) return 1;
    api.kbd_trygetchar_local = local; api.rshell_set_active = active;
    api.sys_halt = halt; api.sys_yield = yield;
    static char longline[160];
    for (int i = 0; i < 140; i++) longline[i] = 'y';
    longline[140] = '\n'; longline[141] = '\033';
    for (mode = 0; mode < 4; mode++) {
        step = polls = halts = yields = resets = on = off = commands = failed = refused = status = 0;
        input = mode == 0 ? "x_\ne_\n" : mode == 1 ? "" : mode == 2 ? "_\033" : longline;
        int rc = rsh_lan_app_loop();
        if (failed || resets != 2 || on != 1 || off != 1 || app_active || sh_exit_flag) return 1;
        if (mode == 0 && (rc != 37 || commands != 2 || polls || halts != 2)) return 1;
        if (mode == 1 && (rc || polls != 1 || commands || halts)) return 1;
        if (mode == 2 && (rc || polls != 1 || yields != 1 || halts != 1)) return 1;
        /* Review P3-1: an over-long USB line is refused with $? = 2 (resident rshell.c). */
        if (mode == 3 && (rc || refused != 1 || status != SH_STATUS_USAGE || commands)) return 1;
    }
    return 0;
}
void _start(void) { die(run()); }
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mutate', action='store_true')
    args = ap.parse_args()
    source = (ROOT / 'userland/shell/rshell_lan.c').read_text()
    app = source[source.index('#ifdef SHELL_REMOTE_APP'):]
    with tempfile.TemporaryDirectory() as td:
        folder = Path(td)
        harness = folder / 'test.c'
        harness.write_text(HARNESS.replace('APP', str(folder / 'app.c')))
        def case(text, expected):
            (folder / 'app.c').write_text(text)
            exe = folder / 'test'
            subprocess.run(['cc', '-m32', '-std=gnu11', '-D__cdecl=', '-DSHELL_REMOTE_APP',
                '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie', '-nostdlib', '-static',
                '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                '-I' + str(ROOT / 'sdk/include/os32'), '-I' + str(ROOT / 'include'),
                '-I' + str(ROOT / 'userland/shell'), str(harness),
                str(ROOT / 'userland/shell/serial_watchdog.c'), '-o', str(exe)], check=True)
            rc = host32.run([str(exe)], timeout=10).returncode
            assert rc == expected, rc
        case(app, 0)
        print('rshell-app: PASS')
        if args.mutate:
            for old, new in [
                ('if (ln.bytes == 0)', 'if (1)'),
                ('int rc = rsh_lan_poll();', 'int rc = RSH_LAN_EXIT;'),
                ('status = sh_exit_code;', 'status = 0;'),
                ('status = sh_exit_code;\n                        sh_exit_flag = 0;', 'status = sh_exit_code;\n                        sh_exit_flag = 1;'),
                ('g_api->rshell_set_active(0);', '(void)0;'),
                ('g_api->sys_halt();', '(void)0;'),
                ('if (rc == OS32_ERR_AGAIN) g_api->sys_yield();', '(void)rc;'),
                ('rsh_lan_reset();\n    app_active = 0;', 'app_active = 0;'),
                ('g_api->rshell_set_active(1);', '(void)0;'),
                ('    sh_exit_flag = 0;\n    return 0;', '    return 0;'),
                ('sh_status_set(SH_STATUS_USAGE); /* refused line', '(void)0; /* refused line'),
            ]:
                assert app.count(old) == 1, old
                case(app.replace(old, new), 1)
                print('RED runtime:', old)


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)

if __name__ == '__main__':
    main()
