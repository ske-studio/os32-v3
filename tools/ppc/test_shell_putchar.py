#!/usr/bin/env python3
"""Check the PowerMac USER character provider and cooperative LAN progress."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'platform/powermac/shell_putchar.c'
CHECK = dict(name='ppc-shell-putchar', tier=1, budget=60, needs=[],
             covers=['platform/powermac/shell_putchar.c', 'tools/ppc/test_shell_putchar.py'],
             inputs=['include/console.h', 'include/types.h', 'kernel/console/console_stream.h',
                     'platform/powermac/lan_boot.h'])
HARNESS = r'''
#include "console.h"
static unsigned char bytes[4];
static unsigned used, polls;
void console_stream_write(const void *p, u32 n) {
    const unsigned char *b = p;
    if (n == 1 && used < sizeof(bytes)) bytes[used++] = *b;
}
void pm_terminal_poll(void) { polls++; }
int main(void) {
    shell_putchar('A', 0); shell_putchar('\0', 255); shell_putchar('\n', 7);
    if (used != 3 || bytes[0] != 'A' || bytes[1] != 0 || bytes[2] != '\n') return 1;
    if (polls != 3) return 2;
    return 0;
}
'''

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    anchor = 'console_stream_write(&ch, 1);'
    if source.count(anchor) != 1:
        raise RuntimeError('mutation anchor is not unique')
    variants = [('normal', source, 0)]
    if args.mutate:
        variants.append(('byte-length', source.replace(anchor, 'console_stream_write(&ch, 0);'), 1))
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        path = Path(tmp)
        (path / 'harness.c').write_text(HARNESS)
        for name, text, expected in variants:
            (path / 'provider.c').write_text(text)
            subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT), '-I', str(ROOT / 'include'),
                            '-I', str(ROOT / 'sdk/include'), '-I', str(ROOT / 'sdk/include/os32'),
                            '-I', str(ROOT / 'platform/powermac'), str(path / 'provider.c'),
                            str(path / 'harness.c'), '-o', str(path / 'test')], check=True)
            result = subprocess.run([str(path / 'test')], timeout=10, check=False)
            if result.returncode != expected:
                raise RuntimeError(f'{name}: expected {expected}, got {result.returncode}')
            print(f'{name}: ' + ('RED' if expected else 'OK'))

if __name__ == '__main__':
    main()

def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
