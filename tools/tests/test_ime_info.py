#!/usr/bin/env python3
"""Run the actual ime info printer against byte-sliced metadata."""
CHECK = dict(name='ime-info', tier=1, needs=[], budget=5, covers=[
    'tools/tests/test_ime_info.py', 'userland/cmds/ime_info.inc',
    'sdk/include/os32/**'])
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIELDS = ('status format_version min_fep_version dict_id kind language charset '
          'variant source source_version license attribution build_tool build_date '
          'entry_count pos_table_version cost_scale mem_reserve_kb cache_pages checksum').split()
HARNESS = r'''
#include "os32/os32api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "printer.inc"
static char text[4096];
static int info(int key, int offset, char *out, int cap)
{
    const char *s = key == IME_DICT_INFO_STATUS ? "meta" :
        key == IME_DICT_INFO_LICENSE || key == IME_DICT_INFO_ATTRIBUTION ? text : "v";
    int len = (int)strlen(s), n = offset < len ? len - offset : 0;
    if (n > cap - 1) n = cap - 1;
    memset(out, 0xa5, cap);
    memcpy(out, s + (offset < len ? offset : len), n);
    out[n] = 0;
    return n;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    int len = atoi(argv[1]);
    /* UTF-8 splits at 255 and the final Latin-1 COPYING characters, encoded
     * as UTF-8. They belong to the source; trailing poison/NUL does not. */
    for (int i = 0; i < len; i++) text[i] = "あ"[i % 3];
    memcpy(text + len, "÷÷\n", 6);
    static KernelAPI api;
    api.ime_dict_info = info;
    return ime_info(&api);
}
'''


def exercise(work, code):
    (work / 'printer.inc').write_text(code)
    (work / 'test.c').write_text(HARNESS)
    exe = work / 'test'
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-D__cdecl=',
                    '-I' + str(ROOT / 'sdk/include'), str(work / 'test.c'),
                    '-o', str(exe)], check=True)
    for length in (0, 1, 249, 250, 251, 254, 255, 256, 504, 505, 506, 510, 3792):
        text = ('あ'.encode() * ((length + 2) // 3))[:length] + '÷÷\n'.encode()
        expected = b''.join(name.encode() + b': ' +
            (b'meta' if name == 'status' else text if name in ('license', 'attribution') else b'v') + b'\n'
            for name in FIELDS)
        result = subprocess.run([str(exe), str(length)], capture_output=True, check=True)
        assert result.stdout == expected, f'byte output differs at length={length}'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    code = (ROOT / 'userland/cmds/ime_info.inc').read_text()
    with tempfile.TemporaryDirectory(prefix='ime-info-') as td:
        work = Path(td)
        exercise(work, code)
        print('GREEN ime info: final chunks, UTF-8 splits, COPYING tail, no NUL/poison', flush=True)
        if args.mutate:
            anchor = 'fwrite(chunk, 1, (size_t)n, stdout);'
            assert code.count(anchor) == 1
            try:
                exercise(work, code.replace(anchor, 'fwrite(chunk, 1, (size_t)n + 1, stdout);'))
            except AssertionError as err:
                print('RED runtime extra byte: ' + str(err), flush=True)
            else:
                raise AssertionError('extra-byte mutant survived')


def run(ctx):
    argv = ['python3', '-B', 'tools/tests/test_ime_info.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)


if __name__ == '__main__':
    main()
