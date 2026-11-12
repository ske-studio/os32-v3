#!/usr/bin/env python3
"""Exercise the PowerMac CLI adapter with terminal and VFS fakes."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'platform/powermac/cli_boot.c'
CHECK = dict(name='ppc-cli-boot', tier=1, budget=60, needs=[],
             covers=['platform/powermac/cli_boot.c', 'platform/powermac/cli_boot.h',
                     'tools/ppc/test_cli_boot.py'],
             inputs=['kernel/cli/resident_cli.c', 'kernel/cli/resident_cli.h',
                     'platform/powermac/lan_boot.h', 'kernel/console/console_stream.h',
                     'fs/vfs.h', 'include/types.h', 'exec/exec.h', 'lib/kstring_c.c', 'lib/kstring.h'])

HARNESS = r'''
#include <setjmp.h>
#include <string.h>
#include "platform/powermac/cli_boot.h"
#include "fs/vfs.h"
static jmp_buf done;
static const char *input;
static char output[4096], path[256], data[256];
static unsigned pos, used, size, reads, writes;
int pm_terminal_getkey(void) {
    if (!input[pos]) longjmp(done, 1);
    return (unsigned char)input[pos++];
}
void pm_terminal_poll(void) { }
void console_stream_write(const void *p, u32 n) {
    if (used + n >= sizeof(output)) longjmp(done, 2);
    memcpy(output + used, p, n); used += n; output[used] = 0;
}
int exec_run(const char *line) { (void)line; return 7; }
int vfs_read(const char *p, void *buf, u32 max) {
    strcpy(path, p); reads++;
    if (max != 1024) return -1;
    memcpy(buf, "motd\n", 5); return 5;
}
int vfs_write(const char *p, const void *buf, u32 n) {
    strcpy(path, p); memcpy(data, buf, n); size = n; writes++; return (int)n;
}
static int check(const char *s, const char *expected) {
    input = s; pos = used = size = reads = writes = 0; output[0] = path[0] = 0;
    int result = setjmp(done);
    if (!result) pm_cli_boot();
    return result == 1 && strcmp(output, expected) == 0;
}
int main(void) {
    if (!check("echo hi\n", "os32> echo hi\nhi\nos32> ")) return 1;
    if (!check("cat /etc/motd\n", "os32> cat /etc/motd\nmotd\nos32> ") ||
        reads != 1 || strcmp(path, "/etc/motd")) return 2;
    if (!check("echo a > /tmp/f\n", "os32> echo a > /tmp/f\nos32> ") ||
        writes != 1 || strcmp(path, "/tmp/f") || size != 2 || memcmp(data, "a\n", 2)) return 3;
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = SOURCE.read_text()
    old = 'console_stream_write(buf, (u32)size);'
    if original.count(old) != 1:
        raise RuntimeError('cat length mutation anchor is not unique')
    variants = [('normal', original, 0)]
    if args.mutate:
        variants.append(('cat-length', original.replace(old, 'console_stream_write(buf, CLI_CAT_BYTES);'), 2))
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        d = Path(tmp)
        (d / 'harness.c').write_text(HARNESS)
        for name, source, expected in variants:
            (d / 'cli_boot.c').write_text(source)
            subprocess.run(['gcc-13', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT), '-I', str(ROOT / 'include'),
                            '-I', str(ROOT / 'sdk/include'), '-I', str(ROOT / 'sdk/include/os32'),
                            '-I', str(ROOT / 'platform/powermac'), str(d / 'cli_boot.c'),
                            str(ROOT / 'kernel/cli/resident_cli.c'), str(ROOT / 'lib/kstring_c.c'), str(d / 'harness.c'),
                            '-o', str(d / 'test')], check=True)
            result = subprocess.run([str(d / 'test')], timeout=10, check=False)
            if result.returncode != expected:
                raise RuntimeError(f'{name}: expected {expected}, got {result.returncode}')
            print(f'{name}: ' + ('OK' if not expected else 'RED'))


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
