#!/usr/bin/env python3
CHECK = dict(name='lantrace', tier=1, needs=[], budget=12,
    covers=['userland/shell/rshell_lan.c', 'userland/shell/rshell.c',
            'userland/gshell/src/lib.rs', 'tools/tests/test_lantrace.py'])
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'userland/shell/rshell_lan.c'
HARNESS = r'''
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "SOURCE"
static KernelAPI api;
KernelAPI *g_api = &api;
static u32 tick;
static char output[4096];
static u32 clock_tick(void) { return tick; }
static void print(u8 attr, const char *fmt, ...) {
    va_list ap; (void)attr; va_start(ap, fmt);
    vsnprintf(output + strlen(output), sizeof(output) - strlen(output), fmt, ap);
    va_end(ap);
}
static int nofile(const char *path, int flags) { (void)path; (void)flags; return -1; }
int main(void) {
    api.get_tick = clock_tick; api.kprintf = print; api.sys_open = nofile;
    trace_active = 1;
    tick = 100; trace_add("first"); tick = 107; trace_add("second");
    for (int i = 0; i < 40; i++) trace_add("extra");
    if (trace_count != 32) return 1;
    rsh_lan_trace_show();
    if (!strstr(output, "first tick=100 delta=0\nsecond tick=107 delta=7\n")) return 1;
    trace_active = 0; trace_add("disabled");
    return trace_count != 32;
}
'''
def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    source = source.replace('"../../include/', '"' + str(ROOT / 'include') + '/')
    source = source.replace('"../lib/host/', '"' + str(ROOT / 'userland/lib/host') + '/')
    with tempfile.TemporaryDirectory(prefix='lantrace-') as td:
        folder = Path(td); src = folder / 'lan.c'; test = folder / 'test.c'; exe = folder / 'test'
        test.write_text(HARNESS.replace('SOURCE', str(src)))
        def run(text):
            src.write_text(text)
            subprocess.run(['cc', '-std=gnu11', '-D__cdecl=', '-ffunction-sections', '-fdata-sections',
                '-I' + str(ROOT / 'sdk/include/os32'), '-I' + str(ROOT / 'userland/shell'),
                '-I' + str(ROOT / 'include'), str(test), '-Wl,--gc-sections', '-o', str(exe)], check=True)
            return subprocess.run([str(exe)]).returncode
        assert run(source) == 0
        print('lantrace: PASS')
        if args.mutate:
            old = 'trace[i].tick - trace[i - 1].tick'
            assert source.count(old) == 1
            assert run(source.replace(old, 'trace[i - 1].tick - trace[i].tick')) == 1
            print('RED runtime: reversed delta')

def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)

if __name__ == '__main__': main()
