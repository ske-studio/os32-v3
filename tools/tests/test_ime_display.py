#!/usr/bin/env python3
"""Check CUI IME reservation and candidate restoration on host TVRAM."""
CHECK = dict(name='ime-display-host', tier=1,
    covers=['kernel/ime/ime_display_private.h', 'platform/pc98/ime_default_render.h', 'platform/pc98/ime_display.c', 'include/ime_control.h', 'include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'drivers/video/pc98/pc98_route.h', 'tools/tests/ime_display_host.c', 'tools/tests/con_sink_host.c', 'tools/tests/host32.py', 'kernel/console/con_sink.c', 'kernel/port_service.c', 'kernel/ime/ime.c', 'kernel/ime/ime_render.inc', 'platform/pc98/ime_render_tvram.c', 'kernel/console/console.c', 'platform/pc98/console.c', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3.1)

from kernel_includes import include_flags
import argparse
from pathlib import Path
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
OLD = '        cursor_y--;\n'
NEW = '        cursor_y -= 0;\n'
MUTATIONS = [
    ('right edge wide replacement', 'ime_render_tvram.c',
     'if (x < TVRAM_COLS - 1) {', 'if (x <= TVRAM_COLS - 1) {',
     'last column rejects wide replacement without writes'),
    ('text allocation amount', 'ime_display.c',
     'kmalloc(IME_CANDLIST_SAVE_ROWS * TVRAM_COLS * sizeof(u16))',
     'kmalloc((IME_CANDLIST_SAVE_ROWS - 1) * TVRAM_COLS * sizeof(u16))', 'text allocation bytes'),
    ('text upper byte restore', 'ime_display.c',
     '*(volatile u16 *)P2V_IO(TVRAM_BASE + offset) = g_cand_save_text[y * TVRAM_COLS + x];',
     '*(volatile u16 *)P2V_IO(TVRAM_BASE + offset) = (u8)g_cand_save_text[y * TVRAM_COLS + x];',
     'mode change restores candidate rows'),
    ('attribute restore', 'ime_display.c',
     '*(volatile u8 *)P2V_IO(TVRAM_ATTR + offset) = g_cand_save_attr[y * TVRAM_COLS + x];',
     '*(volatile u8 *)P2V_IO(TVRAM_ATTR + offset) = 0;', 'candidate restores text and attributes'),
    ('save restore order', 'ime_render.inc',
     'g_cand_has_saved = ime_display_save(top, rows);',
     'g_cand_has_saved = ime_display_restore(top, rows);', 'candidate restores text and attributes'),
    ('partial allocation release', 'ime_display.c',
     'if (g_cand_save_text) kfree(g_cand_save_text);',
     'if (g_cand_save_text && g_cand_save_attr) kfree(g_cand_save_text);', 'abort frees partial resources'),
    ('resource accounting', 'ime_display.c',
     'kmalloc_block_size(g_cand_save_text) + BLK_HDR_SIZE',
     'kmalloc_block_size(g_cand_save_text)', 'resource bytes including headers'),

    ('cursor reservation', 'console.c', OLD, NEW, 'IME reservation preserves cursor'),
    ('mode change restoration', 'ime.c',
     '        candlist_clear(); /* Restore the list before changing input mode. */',
     '', 'mode change restores candidate rows'),
    ('candidate state restoration', 'ime_candidate_commit.inc',
     '    g_ime.state = IME_ST_INPUT;', '',
     'confirmation clears candidate state and saved rows'),
    ('candidate remaining kana redraw', 'ime_candidate_commit.inc',
     '        preedit_draw();', '',
     'confirmation redraws remaining kana or clears preedit'),
    ('candidate empty preedit clear', 'ime_candidate_commit.inc',
     '        preedit_clear();', '', 'preedit cleared'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--runner', choices=('native', 'qemu'))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ime-display-') as directory:
        tmp = Path(directory)
        fixture = (ROOT / 'tools/tests/con_sink_host.c').read_text()
        fixture = fixture.replace('../../kernel/console/console.c', str(tmp / 'console.c'))
        fixture = fixture.replace('../../', str(ROOT) + '/')
        (tmp / 'con_sink_host.c').write_text(fixture)
        (tmp / 'legacy_guest_fake.h').write_text((ROOT / 'tools/tests/legacy_guest_fake.h').read_text())
        source = (ROOT / 'tools/tests/ime_display_host.c').read_text()
        source = source.replace('../../', str(ROOT) + '/')
        source = source.replace(str(ROOT / 'kernel/ime/ime.c'), str(tmp / 'ime.c'))
        source = source.replace(str(ROOT / 'platform/pc98/ime_display.c'), str(tmp / 'ime_display.c'))
        source = source.replace(str(ROOT / 'platform/pc98/ime_render_tvram.c'), str(tmp / 'ime_render_tvram.c'))
        (tmp / 'ime_display_host.c').write_text(source)
        originals = {'console.c': (ROOT / 'kernel/console/console.c').read_text(),
                     'ime_display.c': (ROOT / 'platform/pc98/ime_display.c').read_text(),
                     'ime_render_tvram.c': (ROOT / 'platform/pc98/ime_render_tvram.c').read_text()}
        for name in ('ime.c', 'ime_render.inc', 'ime_convert.inc', 'ime_candidate_commit.inc', 'ime_dict_api.inc'):
            originals[name] = (ROOT / 'kernel/ime' / name).read_text()
        for mutation in [None] + (MUTATIONS if args.mutate else []):
            sources = dict(originals)
            if mutation:
                name, target, old, new, expected = mutation
                assert sources[target].count(old) == 1, 'mutation target must match once'
                sources[target] = sources[target].replace(old, new)
                if name == 'save restore order':
                    restore = 'if (ime_display_restore(top, rows)) g_cand_has_saved = 0;'
                    assert sources[target].count(restore) == 1, 'restore anchor must match once'
                    sources[target] = sources[target].replace(
                        restore, 'if (ime_display_save(top, rows)) g_cand_has_saved = 0;')
            for target, source in sources.items():
                (tmp / target).write_text(source)
            exe = tmp / ('mutant' if mutation else 'normal')
            host32.build(['gcc', '-std=gnu11', '-m32', '-march=i386', '-ffreestanding',
                          '-fno-pie', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror',
                          '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare',
                          '-Wno-type-limits', '-Wno-missing-field-initializers',
                          '-DCON_SINK_NO_IRQ_LOCK', '-D__KERNEL_BUILD__', '-D__cdecl=',
                          '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                          *include_flags(ROOT),
                          '-nostdlib', '-static', '-no-pie', str(tmp / 'ime_display_host.c'),
                          '-o', str(exe)], check=True)
            result = host32.run([str(exe)], runner=args.runner, capture_output=True, text=True, timeout=30)
            print(result.stdout + result.stderr, end='', flush=True)
            if mutation:
                assert result.returncode == 1 and expected in result.stdout
                print('RED runtime ' + name + ' (compiled with -Werror)', flush=True)
            else:
                assert result.returncode == 0, result.returncode


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_ime_display.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
