"""Exercise the PPC shell USER stdio with a short-writing KAPI."""
CHECK = dict(name='ppc-sh-stdio', tier=1, needs=[], budget=10,
    covers=['userland/ppc/sh/stdio.c', 'userland/ppc/sh/include/stdio.h',
            'tools/tests/test_ppc_sh_stdio.py', 'userland/ppc/sh/format.c', 'userland/ppc/sh/runtime.h',
            'sdk/include/os32/*.h'])

import argparse
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'userland/ppc/sh'
HARNESS = r'''
#include <stdio.h>
#include <string.h>
#include "runtime.h"
KernelAPI *kapi;
static char output[512];
static int pos, terminal, wrong_fd;
static void put(char ch, u8 attr) { (void)ch; (void)attr; ++terminal; }
static int write_fd(int fd, const void *buf, u32 size) {
    if (fd != 1) ++wrong_fd;
    if (!size) return 0;
    output[pos++] = *(const char *)buf;
    return 1;
}
int main(void) {
    KernelAPI api = {0};
    char buf[32];
    api.sys_write = write_fd;
    api.shell_putchar = put;
    kapi = &api;
    if (printf("%s %03u", "hello", 7u) != 9) return 1;
    if (terminal || wrong_fd || pos != 9 || memcmp(output, "hello 007", 9)) return 2;
    if (setvbuf(stdout, 0, _IOLBF, BUFSIZ) || fflush(stdout)) return 3;
    if (snprintf(buf, 5, "%s", "hello") != 5 || strcmp(buf, "hell")) return 4;
    if (sprintf(buf, "%d", -42) != 3 || strcmp(buf, "-42")) return 5;
    return 0;
}
'''


def exercise(folder, mutate=False):
    source = (SRC / 'stdio.c').read_text()
    if mutate:
        old = 'sent += (size_t)n;'
        if source.count(old) != 1:
            raise RuntimeError('mutation anchor is not unique')
        source = source.replace(old, 'sent = out->used;')
    (folder / 'stdio.c').write_text(source)
    (folder / 'test.c').write_text(HARNESS)
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-attributes', '-fno-builtin', '-I' + str(SRC / 'include'),
                    '-I' + str(SRC), '-I' + str(ROOT / 'sdk/include/os32'),
                    str(folder / 'stdio.c'), str(SRC / 'format.c'),
                    str(folder / 'test.c'), '-o', str(folder / 'test')], check=True)
    result = subprocess.run([str(folder / 'test')], check=False)
    expected = 2 if mutate else 0
    if result.returncode != expected:
        raise RuntimeError(f'expected {expected}, got {result.returncode}')


def run(ctx):
    argv = ['python3', '-B', 'tools/tests/test_ppc_sh_stdio.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        exercise(pathlib.Path(temp))
        if args.mutate:
            exercise(pathlib.Path(temp), True)
    print('ppc sh stdio PASS')


if __name__ == '__main__':
    main()
