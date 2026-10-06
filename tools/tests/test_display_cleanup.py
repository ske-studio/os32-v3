"""Display cleanup: real V86 exits and banked splash; runtime-only mutants.

Record: tools/tests/display_cleanup_tdd.md
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
TARGET_SRCS = ('kernel/v86_io.c', 'kernel/v86_gcap.c', 'kernel/boot_splash.c', 'gfx/gfx_core.c')


def function(text, name):
    match = re.search(r'^(?:static )?void ' + name + r'\([^\n]*\)\n\{', text, re.M)
    if not match:
        raise AssertionError('missing function: ' + name)
    start = match.start()
    pos = match.end()
    depth = 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos] + '\n'


def build_run(tree, kind, cases):
    with tempfile.TemporaryDirectory(prefix='display-exe-') as td:
        tmp = Path(td)
        if kind == 'v86':
            io = (tree / TARGET_SRCS[0]).read_text()
            gcap = (tree / TARGET_SRCS[1]).read_text()
            names = ['io_out']
            if 'void v86_cui_display_restore(' in io:
                names.append('v86_cui_display_restore')
            body = ''.join(function(io, name) for name in names + ['gfx_state_for_os32', 'v86_io_reset_policy'])
            if 'static void gcap_out(' in gcap:
                body += function(gcap, 'gcap_out')
            body += function(gcap, 'gcap_cui_rebuild')
            (tmp / 'display_cleanup_slice.inc').write_text(body)
            src = tree / 'tools/tests/display_cleanup_host.c'
        else:
            src = tree / 'tools/tests/boot_splash_native_host.c'
        binary = tmp / 'test'
        command = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-function', '-D__KERNEL_BUILD__', '-Wl,--gc-sections',
                   '-I' + str(tmp)]
        command += ['-I' + str(tree / p) for p in
                    ('include', 'sdk/include', 'sdk/include/os32', 'gfx', 'lib', 'kernel')]
        subprocess.run(command + [str(src), '-o', str(binary)], check=True)
        return [host32.run([str(binary), *args], capture_output=True, text=True, timeout=60)
                for args in cases]


# Each anchor's exact hit count is fixed; compile failures never count as RED.
MUTATIONS = [
    ('v86-height', TARGET_SRCS[0], '    gfx_current_height = GFX_HEIGHT;', '', 1, 'v86', 'FAIL: CUI height flip'),
    ('v86-flip', TARGET_SRCS[0], '    gfx_flip_enabled = 0;', '', 1, 'v86', 'FAIL: CUI height flip'),
    ('v86-start', TARGET_SRCS[0], '    v86_cui_display_restore();',
     '    io_out(GDC_GFX_CMD, GDC_CMD_START);', 1, 'v86', 'FAIL: graphics must stay stopped'),
    ('v86-access-one', TARGET_SRCS[0], 'io_out(GDC_ACCESS_PAGE, GDC_PAGE_0);\n\n    /* 16 色',
     'io_out(GDC_ACCESS_PAGE, GDC_PAGE_1);\n\n    /* 16 色', 1, 'v86', 'FAIL: access page must be zero'),
    ('v86-display-one', TARGET_SRCS[0], 'v86_cui_display_restore();\n    io_out(GDC_DISP_PAGE, GDC_PAGE_0);',
     'v86_cui_display_restore();\n    io_out(GDC_DISP_PAGE, GDC_PAGE_1);', 1, 'v86', 'FAIL: display page must be zero'),
    ('no-display-enable', TARGET_SRCS[0], '    io_out(MODE_FF1_PORT, MFF1_DISP_ON);', '', 1, 'v86', 'FAIL: display output must be enabled'),
    ('no-text-start', TARGET_SRCS[0], '    io_out(GDC_TEXT_CMD, GDC_CMD_START);', '', 1, 'v86', 'FAIL: text must be started'),
    ('gcap-no-restore', TARGET_SRCS[1], '    v86_cui_display_restore();', '', 1, 'v86', 'FAIL: capture rebuild'),
    ('old-splash-cleanup', TARGET_SRCS[2], '    gfx_clear_planar_pages(GFX_PLANE_SZ);',
     '    bb_clear(0);\n    gfx_add_dirty_rect(0, 0, GFX_WIDTH, GFX_HEIGHT);\n    gfx_present();', 1, 'splash', 'FAIL: residual logo page=0'),
    ('clear-page-zero-twice', TARGET_SRCS[3], '    _out(GDC_ACCESS_PAGE, GDC_PAGE_1);\n    kmemset',
     '    _out(GDC_ACCESS_PAGE, GDC_PAGE_0);\n    kmemset', 1, 'splash', 'FAIL: residual logo page=1'),
    ('clear-page-one-twice', TARGET_SRCS[3], '    _out(GDC_ACCESS_PAGE, GDC_PAGE_0);\n    kmemset',
     '    _out(GDC_ACCESS_PAGE, GDC_PAGE_1);\n    kmemset', 1, 'splash', 'FAIL: residual logo page=0'),
    ('clear-half-plane', TARGET_SRCS[2], 'gfx_clear_planar_pages(GFX_PLANE_SZ);',
     'gfx_clear_planar_pages(GFX_PLANE_SZ_200);', 1, 'splash', 'FAIL: residual logo page=0'),
]
MUTATIONS += [
    ('init-no-clear', TARGET_SRCS[3], '    gfx_clear_planar_pages(GFX_PLANE_SZ);',
     '', 1, 'splash', 'FAIL: gfx_init residual page=0'),
    ('init-200-no-clear', TARGET_SRCS[3], '    gfx_clear_planar_pages(GFX_PLANE_SZ_200);',
     '', 1, 'splash', 'FAIL: gfx_init_200 residual page=0'),
]
for plane in 'BRGI':
    MUTATIONS.append(('omit-plane-' + plane, TARGET_SRCS[3],
                      f'    kmemset((u8 *)P2V(VRAM_PLANE_{plane}), 0, plane_size);', '', 2, 'splash',
                      'FAIL: residual logo page=0 plane=' + str('BRGI'.index(plane))))


def main():
    cases = {'v86': [[str(i)] for i in range(3)],
             'splash': [[str(i), *fault] for fault in ([], ['fault']) for i in range(4)] + [['1', 'init400'], ['1', 'init200']]}
    for kind in cases:
        results = build_run(ROOT, kind, cases[kind])
        for result in results:
            assert result.returncode == 0, result.stdout + result.stderr
        print(f'{kind}: {len(results)} runtime cases PASS')
    if '--mutate' not in sys.argv:
        return
    with tempfile.TemporaryDirectory(prefix='display-mut-') as td:
        tree = Path(td)
        # Only source/header files, never build outputs or hardware documents.
        for directory in ('include', 'sdk/include', 'gfx', 'kernel', 'lib'):
            for source in (ROOT / directory).rglob('*'):
                if source.is_file() and source.suffix in ('.h', '.c'):
                    target = tree / source.relative_to(ROOT)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source, target)
        (tree / 'exec').mkdir(parents=True)
        for name in ('surface_query.h', 'lease.h', 'appslot.h'):
            shutil.copyfile(ROOT / 'exec' / name, tree / 'exec' / name)
        (tree / 'tools/tests').mkdir(parents=True)
        for name in ('display_cleanup_host.c', 'boot_splash_native_host.c'):
            shutil.copyfile(ROOT / 'tools/tests' / name, tree / 'tools/tests' / name)
        for name, path, old, new, hits, kind, expected_fail in MUTATIONS:
            target = tree / path
            original = target.read_text()
            assert original.count(old) == hits, (name, original.count(old), hits)
            target.write_text(original.replace(old, new))
            try:
                results = build_run(tree, kind, cases[kind])
                assert any(r.returncode == 1 and expected_fail in r.stderr for r in results), name + ' survived'
                print(name + ': runtime RED')
            finally:
                target.write_text(original)
    print(f'{len(MUTATIONS)} mutants: runtime RED')


if __name__ == '__main__':
    main()
