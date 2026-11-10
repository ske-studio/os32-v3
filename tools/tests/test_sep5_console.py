#!/usr/bin/env python3
"""C1: the console takes its grid, default colour and route guard from the backend."""
CHECK = dict(name='sep5-console-host', tier=1, needs=[], budget=10,
    covers=['kernel/console/console.c', 'kernel/console/console_backend.h',
            'platform/pc98/console.c', 'platform/pc98/tvram_private.h', 'include/tvram.h',
            'drivers/video/pc98/pc98_route.h', 'lib/utf8.c', 'lib/utf8.h',
            'tools/tests/sep5_console_host.c', 'tools/tests/sep5_console_pc98_host.c',
            'tools/tests/test_sep5_console.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk', 'include/**',
            'arch/x86/**', 'platform/pc98/**', 'kernel/console/**', 'sdk/include/os32/**'])
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
CONSOLE = 'kernel/console/console.c'
BACKEND = 'platform/pc98/console.c'
# (name, unique anchor, replacement, exit code of the first expectation that must
# fail). 98/99 = the console left the backend's grid. Codes end in the grid index
# (0 = 80x25, 1 = 40x10): a stale 80/25 constant only shows on the second grid.
MUTATIONS = (
    ('size-cols-80', '    if (w) *w = CON_COLS;', '    if (w) *w = 80;', 11),
    ('size-rows-25', '    if (h) *h = CON_ROWS;', '    if (h) *h = 25;', 11),
    ('wrap-80', '    if (cursor_x >= CON_COLS) {\n        cursor_x = 0;\n        cursor_y++;\n    }\n    if (cursor_y > CONSOLE_LAST_ROW) {',
     '    if (cursor_x >= 80) {\n        cursor_x = 0;\n        cursor_y++;\n    }\n    if (cursor_y > CONSOLE_LAST_ROW) {', 99),
    ('clamp-x-80', '    if (x >= CON_COLS) x = CON_COLS - 1;', '    if (x >= 80) x = 79;', 98),
    ('clamp-y-25', '    if (y >= CON_ROWS) y = CON_ROWS - 1;', '    if (y >= 25) y = 24;', 98),
    ('last-row-25', '#define CONSOLE_LAST_ROW  (CON_ROWS - 1 - g_scroll_reserve)',
     '#define CONSOLE_LAST_ROW  (25 - 1 - g_scroll_reserve)', 98),
    ('scroll-25', '    con_backend_scroll(CON_ROWS - g_scroll_reserve);', '    con_backend_scroll(25 - g_scroll_reserve);', 41),
    ('reserve-clamp-25', '    if (rows >= CON_ROWS) {\n        rows = CON_ROWS - 1;', '    if (rows >= 25) {\n        rows = 24;', 51),
    ('reserve-move-25', '        con_backend_scroll(CON_ROWS);', '        con_backend_scroll(25);', 55),
    ('reserve-row-25', '        cursor_y == CON_ROWS - 1 && console_render_allowed()) {',
     '        cursor_y == 25 - 1 && console_render_allowed()) {', 55),
    ('wide-wrap-80', '    if (jis) {\n        if (cursor_x >= CON_COLS - 1) {', '    if (jis) {\n        if (cursor_x >= 80 - 1) {', 99),
    ('replacement-wrap-80', '    /* 変換不可: □を表示 */\n    if (cursor_x >= CON_COLS - 1) {',
     '    /* 変換不可: □を表示 */\n    if (cursor_x >= 80 - 1) {', 99),
    ('replacement-code', 'CON_JIS_REPLACEMENT, color);', '0x2221, color);', 70),
    ('erase-caller-colour', "' ', CON_ATTR_DEFAULT);", "' ', color);", 80),
    ('render-ungated', '    return con_backend_safe() &&\n', '    return 1 &&\n', 94),
    ('clear-ungated', 'void tvram_clear(void)\n{\n    if (!con_backend_safe()) return;\n', 'void tvram_clear(void)\n{\n', 90),
    ('read-ungated', '    if (!con_backend_safe()) return;\n    con_backend_readchar(x, y, code, attr);', '    con_backend_readchar(x, y, code, attr);', 90),
    ('reverse-ungated', '    if (!con_backend_safe()) return 0;\n    return con_backend_reverse(x, y);', '    return con_backend_reverse(x, y);', 90),
    ('cursor-enable-ungated', 'void console_hw_cursor_enable(void)\n{\n    if (!con_backend_safe()) return;\n', 'void console_hw_cursor_enable(void)\n{\n', 90),
    ('gdc-stop-ungated', 'void console_text_gdc_stop(void)\n{\n    if (!con_backend_safe()) return;\n', 'void console_text_gdc_stop(void)\n{\n', 90),
    ('scroll-ungated', 'void tvram_scroll(void)\n{\n    if (!con_backend_safe()) return;\n', 'void tvram_scroll(void)\n{\n', 90),
)
# The PC-98 backend: (name, anchor, replacement, exit code, built without the guard?)
BACKEND_MUTATIONS = (
    ('pc98-grid-swapped', '{ TVRAM_COLS, TVRAM_ROWS }', '{ TVRAM_ROWS, TVRAM_COLS }', 2, False),
    ('pc98-colour', '    return ATTR_WHITE;\n', '    return 0;\n', 3, False),
    ('pc98-guard-ignored', '    return !pc98_console_safe || pc98_console_safe();', '    return 1;', 4, False),
    ('pc98-guard-required', '    return !pc98_console_safe || pc98_console_safe();',
     '    return pc98_console_safe && pc98_console_safe();', 5, True),
)
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-function', '-Wno-sign-compare', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
         '-nostdlib', '-static', '-no-pie', '-D__KERNEL_BUILD__']


def execute(text, work):
    (work / 'console_source.inc').write_text(text)
    exe = work / 'console'
    subprocess.run(['gcc', *FLAGS, '-I' + str(work), *include_flags(ROOT),
        str(ROOT / 'tools/tests/sep5_console_host.c'), str(ROOT / 'lib/utf8.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def backend_slice(text):
    start = text.index('/* 標準の 80×25。')
    return text[start:text.index('void con_backend_clear(void)')]


def execute_backend(text, work, unguarded=False):
    (work / 'console_pc98_slice.inc').write_text(backend_slice(text))
    exe = work / 'console-pc98'
    subprocess.run(['gcc', *FLAGS, '-Wno-address', *(['-DNO_GUARD'] if unguarded else []),
        '-I' + str(work), *include_flags(ROOT),
        str(ROOT / 'tools/tests/sep5_console_pc98_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def boundary():
    """The common console names no PC-98 header, address or fixed grid any more."""
    console = (ROOT / CONSOLE).read_text()
    for word in ('pc98', 'TVRAM_COLS', 'TVRAM_ROWS', 'TATTR_', 'TVRAM_BASE', 'TVRAM_ATTR'):
        assert word not in console, word
    header = (ROOT / 'include/tvram.h').read_text()
    assert '#include "pc98.h"' not in header and '#define' not in header.replace('#define __TVRAM_H', '')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    console, backend = (ROOT / CONSOLE).read_text(), (ROOT / BACKEND).read_text()
    boundary()
    with tempfile.TemporaryDirectory(prefix='sep5-console-') as directory:
        work = Path(directory)
        assert execute(console, work) == 0
        assert execute_backend(backend, work) == 0
        assert execute_backend(backend, work, unguarded=True) == 0
        print('PASS console on 80x25, 40x10 and 132x50 backends (size, wrap, clamp, scroll, '
              'reserved rows, wide/replacement at the edge, default colour, route guard) and '
              'the PC-98 backend values', flush=True)
        if args.mutate:
            for name, old, new, code in MUTATIONS:
                assert console.count(old) == 1, (name, console.count(old))
                rc = execute(console.replace(old, new), work)
                assert rc == code, (name, rc, code)
                print('MUTATE runtime RED %s (exit %d)' % (name, code), flush=True)
            for name, old, new, code, unguarded in BACKEND_MUTATIONS:
                assert backend.count(old) == 1, (name, backend.count(old))
                rc = execute_backend(backend.replace(old, new), work, unguarded)
                assert rc == code, (name, rc, code)
                print('MUTATE runtime RED %s (exit %d)' % (name, code), flush=True)
            assert execute(console, work) == 0


def run(ctx):
    import os
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_sep5_console.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    main()
