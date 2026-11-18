"""Expectation origin: PPC_FP_NEWLIB_DESIGN §4 N3・§8 / astra レビュー 2026-11-18."""
CHECK = dict(name='ppc-sh-runtime', tier=1, needs=[], budget=10,
    covers=['userland/ppc/sh/*', 'tools/tests/ppc_sh_runtime.py','sdk/include/os32/os32api.h', 'sdk/include/os32/os32_kapi_shared.h',
            'sdk/include/os32/os32_kapi_generated.h'])

import argparse
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'userland/ppc/sh'
HARNESS = r'''
#include <limits.h>
#include <string.h>
#include "runtime.h"
KernelAPI *kapi;
static char output[4096];
static int pos, writes, utf8_calls, wrong_attr;
static u8 utf8_color;
static void utf8(const char *s, u8 attr) {
    ++utf8_calls; utf8_color = attr;
    while (*s) output[pos++] = *s++;
    output[pos] = 0;
}
static void put(char ch, u8 attr) { if (attr != 7) ++wrong_attr; output[pos++] = ch; output[pos] = 0; }
static int write_fd(int fd, const void *buf, u32 size) {
    (void)fd; (void)buf; ++writes; return (int)size;
}
int main(void) {
    KernelAPI original = {0}, snapshot, *api;
    original.shell_putchar = put;
    original.sys_write = write_fd;
    original.shell_print_utf8 = utf8;
    snapshot = original;
    api = sh_runtime_init(&original);
    if (api == &original || api != kapi || memcmp(&snapshot, &original, sizeof original)) return 1;
    api->kprintf(7, "%s %u %d %02u %03u %08x %c %%", "ok", 42u, INT_MIN, 4u, 5u, 0xabu, 'Z');
    if (writes || wrong_attr || strcmp(output, "ok 42 -2147483648 04 005 000000ab Z %")) return 2;
    if (api->shell_print_utf8 != original.shell_print_utf8) return 3;
    api->shell_print_utf8(" 日本語", 9);
    if (writes || utf8_calls != 1 || utf8_color != 9 || strcmp(output, "ok 42 -2147483648 04 005 000000ab Z % 日本語")) return 3;
    {   /* help.c passes unbounded lines through "    %s\n": nothing may be cut. */
        static char line[1500];
        memset(line, 'a', sizeof line - 1);
        pos = 0; output[0] = 0;
        api->kprintf(7, "    %s\n", line);
        if (pos != 4 + (int)(sizeof line - 1) + 1 || output[pos - 1] != '\n') return 4;
    }
    return 0;
}
'''


def exercise(folder, mutation=None):
    runtime = (SRC / 'newlib_runtime.c').read_text()
    if mutation:
        old, new, expected = mutation
        if runtime.count(old) != 1:
            raise RuntimeError('mutation anchor is not unique')
        runtime = runtime.replace(old, new)
    else:
        expected = 0
    (folder / 'runtime.c').write_text(runtime)
    (folder / 'test.c').write_text(HARNESS)
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-attributes', '-I' + str(SRC),
                    '-I' + str(ROOT / 'sdk/include/os32'),
                    str(folder / 'runtime.c'),
                    str(folder / 'test.c'), '-o', str(folder / 'test')], check=True)
    result = subprocess.run([str(folder / 'test')], check=False)
    if result.returncode != expected:
        raise RuntimeError(f'expected {expected}, got {result.returncode}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        folder = pathlib.Path(temp)
        exercise(folder)
        if args.mutate:
            for old, new, expected in (
                ('    /* Keep the kernel UTF-8 endpoint: bytewise putchar cannot decode glyphs. */',
                 '    runtime_api.shell_print_utf8 = 0;', 3),
                ('runtime_api.shell_putchar(text[i], attr);',
                 '(void)attr, runtime_api.sys_write(1, &text[i], 1);', 2),
                ('runtime_api.shell_putchar(text[i], attr);',
                 'runtime_api.shell_putchar(text[i], attr ^ 1);', 2),
                ('i < size; ++i)', 'i < size && i < 1023; ++i)', 4),
            ):
                exercise(folder, (old, new, expected))
                print('runtime RED:', old)
    print('ppc sh runtime PASS')


if __name__ == '__main__':
    main()
