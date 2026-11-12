"""Exercise the PPC shell's actual USER formatting and output adapters."""
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
static char output[2048];
static int pos, writes;
static void put(char ch, u8 attr) { (void)attr; output[pos++] = ch; output[pos] = 0; }
static int write_fd(int fd, const void *buf, u32 size) {
    (void)fd; (void)buf; ++writes; return (int)size;
}
int main(void) {
    KernelAPI original = {0}, snapshot, *api;
    original.shell_putchar = put;
    original.sys_write = write_fd;
    snapshot = original;
    api = sh_runtime_init(&original);
    if (api == &original || api != kapi || memcmp(&snapshot, &original, sizeof original)) return 1;
    api->kprintf(7, "%s %u %d %02u %03u %08x %c %%", "ok", 42u, INT_MIN, 4u, 5u, 0xabu, 'Z');
    if (writes || strcmp(output, "ok 42 -2147483648 04 005 000000ab Z %")) return 2;
    api->shell_print_utf8(" UTF8", 9);
    if (writes || strcmp(output, "ok 42 -2147483648 04 005 000000ab Z % UTF8")) return 3;
    return 0;
}
'''


def exercise(folder, mutation=None):
    runtime = (SRC / 'runtime.c').read_text()
    formatter = (SRC / 'format.c').read_text()
    if mutation:
        target, old, new, expected = mutation
        source = runtime if target == 'runtime' else formatter
        if source.count(old) != 1:
            raise RuntimeError('mutation anchor is not unique')
        source = source.replace(old, new)
        if target == 'runtime':
            runtime = source
        else:
            formatter = source
    else:
        expected = 0
    (folder / 'runtime.c').write_text(runtime)
    (folder / 'format.c').write_text(formatter)
    (folder / 'test.c').write_text(HARNESS)
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-attributes', '-I' + str(SRC),
                    '-I' + str(ROOT / 'sdk/include/os32'),
                    str(folder / 'runtime.c'), str(folder / 'format.c'),
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
            exercise(folder, ('runtime', 'runtime_api.shell_putchar(*p, attr);',
                              '(void)attr, runtime_api.sys_write(1, p, 1);', 2))
            exercise(folder, ('format', "emit(out, size, &count, '0');",
                              "emit(out, size, &count, ' ');", 2))
    print('ppc sh runtime PASS')


if __name__ == '__main__':
    main()
