#!/usr/bin/env python3
"""Exercise the PowerMac shell boot loop with controlled execution results."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-sh-boot', tier=1, budget=60, needs=[],
             covers=['platform/powermac/shell_boot.c', 'platform/powermac/shell_boot.h',
                     'platform/powermac/exec_boot.c', 'platform/powermac/lan_terminal.c',
                     'arch/ppc/exec_context.c', 'arch/ppc/arch_exec_storage.h'],
             inputs=['tools/tests/ppc_sh_boot.py', 'platform/powermac/lan_boot.h',
                     'exec/exec.h', 'include/kprintf.h', 'kernel/console/console_stream.h'])
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exec.h"
#include "types.h"
#include "shell_boot.h"
static int polls, calls, pending, writes, cancel;
static void fail(int code, const char *message) { fprintf(stderr, "%s\n", message); exit(code); }
int pm_term_connected(void) { if (pending) fail(13, "order-fail"); return polls >= 2; }
void pm_terminal_poll(void) {
    if (pending) fail(13, "order-fail");
    if (++polls == 4) {
        if (calls != 3 || writes != 3 || cancel != 1) fail(12, "failure-fail");
        fprintf(stderr, "failure-ok\n"); exit(0);
    }
}
int pm_terminal_trygetkey(void) { if (pending) fail(13, "order-fail"); ++cancel; return 27; }
int exec_run(const char *cmd) {
    if (pending) fail(13, "order-fail");
    if (polls < 2 || cancel != 1) fail(11, "connect-fail");
    if (strcmp(cmd, "/bin/sh.bin")) fail(11, "connect-fail");
    if (++calls > 3) fail(12, "failure-fail");
    pending = 1; return 0;
}
int exec_last_result(int *kind, int *code) {
    if (!pending) fail(13, "order-fail");
    pending = 0;
    *kind = calls == 3 ? EXEC_KIND_NOT_FOUND : EXEC_KIND_EXITED;
    *code = calls == 2 ? 3 : 0;
    if (calls == 1) fprintf(stderr, "connect-ok\n");
    if (calls == 3) fprintf(stderr, "restart-ok\n");
    return 0;
}
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; if (pending) fail(13, "order-fail"); ++writes; }
int kutoa_dec(u32 value, char *buf, int size) { if (pending) fail(13, "order-fail"); return snprintf(buf, (size_t)size, "%u", (unsigned)value); }
void console_stream_write(const void *buf, u32 len) { (void)buf; (void)len; if (pending) fail(13, "order-fail"); }
int main(void) { pm_shell_boot(); return 99; }
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'platform/powermac/shell_boot.c').read_text()
    mutations = [('connect', '    while (!pm_term_connected()) pm_terminal_poll();', '    if (0) pm_terminal_poll();', 11, 'connect-fail\n'),
                 ('failure', '        if (kind != EXEC_KIND_EXITED) break;', '        if (kind != EXEC_KIND_EXITED) continue;', 12, 'connect-ok\nrestart-ok\nfailure-fail\n')]
    cases = [('normal', source, 0, 'connect-ok\nrestart-ok\nfailure-ok\n')]
    for name, old, new, status, marker in mutations:
        if source.count(old) != 1:
            raise RuntimeError(f'{name}: mutation anchor is not unique')
        if args.mutate:
            cases.append((name, source.replace(old, new), status, marker))
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        path = Path(tmp)
        (path / 'harness.c').write_text(HARNESS)
        for name, code, status, marker in cases:
            (path / 'boot.c').write_text(code)
            subprocess.run(['gcc', '-std=gnu11', '-D__cdecl=', '-Wall', '-Wextra', '-Werror', '-I.', '-Iinclude',
                            '-Iexec', '-Iplatform/powermac', '-Isdk/include/os32', str(path / 'harness.c'),
                            str(path / 'boot.c'), '-o', str(path / 'test')], cwd=ROOT, check=True)
            result = subprocess.run([str(path / 'test')], capture_output=True, text=True, timeout=10)
            if result.returncode != status or result.stderr != marker:
                raise RuntimeError(f'{name}: exit {result.returncode}: {result.stderr!r}')
            print(('OK ' if name == 'normal' else 'RED ') + name, flush=True)


if __name__ == '__main__':
    main()
