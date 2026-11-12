#!/usr/bin/env python3
"""Exercise the resident CLI with a fake terminal and filesystem."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'kernel/cli/resident_cli.c'
CHECK = dict(name='resident-cli', tier=1, budget=60, needs=[],
             covers=['kernel/cli/resident_cli.c', 'kernel/cli/resident_cli.h',
                     'include/types.h', 'tools/tests/test_resident_cli.py',
                     'lib/kstring_c.c', 'lib/kstring.h'])

HARNESS = r'''
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "kernel/cli/resident_cli.h"
static jmp_buf done;
static const char *input;
static char out[4096], path[256], data[256];
static unsigned pos, used, size;
static int special, polled;
static int getkey(void) {
    if (special && pos == 8) { special = 0; return RESIDENT_CLI_KEY_CANCEL; }
    if (polled == -1) { polled = 0; return -2; }
    if (!input[pos]) longjmp(done, 1);
    return (unsigned char)input[pos++];
}
static void write_out(const void *p, u32 n) { memcpy(out + used, p, n); used += n; out[used] = 0; }
static void poll_key(void) { polled++; }
static int cat_file(const char *p) { (void)p; return 0; }
static int put_file(const char *p, const void *d, u32 n) {
    strcpy(path, p); memcpy(data, d, n); size = n; return 0;
}
static char invoked[256];
static int calls;
static int run_line(const char *line) { strcpy(invoked, line); calls++; return calls == 1 ? 7 : -42; }
static const struct resident_cli_ops ops = { getkey, write_out, poll_key, cat_file, put_file, 0 };
static const struct resident_cli_ops run_ops = { getkey, write_out, poll_key, cat_file, put_file, run_line };
static int check(const char *s, const char *expected) {
    input = s; pos = used = size = 0; out[0] = path[0] = 0;
    if (!setjmp(done)) resident_cli_run(&ops);
    return strcmp(out, expected) == 0;
}
int main(void) {
    char longline[270], expected[400];
    if (!check("echo hi\necho ok\n", "os32> echo hi\nhi\nos32> echo ok\nok\nos32> ")) return 1;
    if (!check("echo ax\b\177b\n", "os32> echo ax\b \b\b \bb\nb\nos32> ")) return 2;
    if (!check("echo bad\003echo good\n", "os32> echo bad\nos32> echo good\ngood\nos32> ")) return 3;
    memset(longline, 'x', 256); strcpy(longline + 256, "\necho z\n");
    strcpy(expected, "os32> "); memset(expected + 6, 'x', 255);
    strcpy(expected + 261, "\nline too long\nos32> echo z\nz\nos32> ");
    if (!check(longline, expected)) return 4;
    if (!check("echo a > /tmp/f\n", "os32> echo a > /tmp/f\nos32> ") ||
        strcmp(path, "/tmp/f") || size != 2 || memcmp(data, "a\n", 2)) return 5;
    if (!check("nope\n", "os32> nope\nunknown command\nos32> ")) return 6;
    special = 1; polled = -1;
    if (!check("echo badecho good\n", "os32> echo bad\nos32> echo good\ngood\nos32> ") ||
        polled != 3) return 7;
    memset(longline, 'x', 256); strcpy(longline + 256, "\003echo z\n");
    strcpy(expected, "os32> "); memset(expected + 6, 'x', 255);
    strcpy(expected + 261, "\nos32> echo z\nz\nos32> ");
    if (!check(longline, expected)) return 8;
    calls = 0; input = "  /bin/hello.bin  ok \nmissing arg\nhelp\necho hi\ncat /x\necho a > /tmp/f\n";
    pos = used = 0; out[0] = 0;
    if (!setjmp(done)) resident_cli_run(&run_ops);
    if (calls != 2 || strcmp(invoked, "missing arg") ||
        !strstr(out, "exit=7\nos32> ") || !strstr(out, "exit=-42\nos32> ")) return 9;
    calls = 0; input = "  /bin/hello.bin  ok \n"; pos = used = 0; out[0] = 0;
    if (!setjmp(done)) resident_cli_run(&run_ops);
    if (calls != 1 || strcmp(invoked, "  /bin/hello.bin  ok ")) return 10;
    return 0;
}
'''
MUTATIONS = [
    ('split-line', 'ops->run(original)', 'ops->run(line)', 9),
    ('reset', '            ops->poll();\n            length = 0;',
     '            ops->poll();\n            /* missing reset */', 1),
    ('space', 'char *path = spaces(redirect + 1), *end = redirect;',
     'char *path = redirect + 1, *end = redirect;', 5),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = SOURCE.read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        d = Path(tmp)
        (d / 'harness.c').write_text(HARNESS)
        variants = [('normal', original, 0)]
        if args.mutate:
            for name, old, new, code in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError(f'{name}: mutation anchor is not unique')
                variants.append((name, original.replace(old, new), code))
        for name, source, expected in variants:
            (d / 'resident_cli.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'gcc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT), '-I', str(ROOT / 'include'),
                            '-I', str(ROOT / 'kernel/cli'), str(d / 'resident_cli.c'),
                            str(d / 'harness.c'), str(ROOT / 'lib/kstring_c.c'), '-o', str(d / 'test')], check=True)
            result = subprocess.run([str(d / 'test')], timeout=10, check=False)
            if result.returncode != expected:
                raise RuntimeError(f'{name}: expected {expected}, got {result.returncode}')
            print(f'{name}: ' + ('OK' if not expected else 'RED'))


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
