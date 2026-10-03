"""Real console/FM bounds regression. Copies only for mutations.
Record: tools/tests/kapi_bounds_tdd.md.
"""
import argparse
import pathlib
import subprocess
import tempfile
import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = ('kernel/console.c', 'drivers/fm.c', 'kapi/kapi_db.c', 'gfx/gfx_core.c', 'gfx/gfx_vram.c')
GUARD = '    if (x < 0 || x >= TVRAM_COLS || y < 0 || y >= TVRAM_ROWS) return;\n'
MUTATIONS = [
    ('putchar', 0, 'tvram_putchar_at', GUARD, '', 'putchar bounds'),
    ('readchar', 0, 'tvram_readchar_at', GUARD, '', 'readchar bounds'),
    ('kanji', 0, 'tvram_putkanji_at', GUARD.replace('x >= TVRAM_COLS', 'x >= TVRAM_COLS - 1'), '', 'kanji bounds'),
    ('tone', 1, 'fm_set_tone_num', 'tone_num >= 0 && ', '', 'tone bounds'),
    ('note', 1, 'fm_note_on', '    if (note < 0) return;\n', '', 'note bounds'),
    ('db-col', 2, 'kapi_db_column_text', '(u32)col >=\n        (DB_SHM_RESULT_LIMIT - sizeof(DB_ResultHeader)) / sizeof(DB_ColumnInfo) ||\n        ', '', 'DB column bounds'),
    ('db-offset', 2, 'kapi_db_column_text', 'info->data_offset <= 0 || (u32)info->data_offset >= DB_SHM_RESULT_LIMIT', 'info->data_offset == 0', 'DB offset bounds'),
    ('palette16', 3, 'gfx_lease_palette', 'first >= PALETTE_COUNT || count > PALETTE_COUNT - first', 'first + count > PALETTE_COUNT', 'palette count bounds'),
    ('palette256', 3, 'gfx_lease_palette', 'first > (int)si.lease_first + (int)si.lease_count ||\n            count > (int)si.lease_first + (int)si.lease_count - first', 'first + count > (int)si.lease_first + (int)si.lease_count', 'palette first bounds'),
    ('dirty', 4, 'gfx_add_dirty_rect', '    if (!gfx_clip_screen(&x, &y, &w, &h, GFX_WIDTH, gfx_current_height)) return;\n', '', 'dirty clipped'),
    ('raster', 4, 'gfx_present_raster', 'table->count <= 0 || table->count > GFX_RASTER_MAX_ENTRIES', 'table->count == 0', 'raster negative count'),

]

def run(runner, mutation=None):
    sources = [(ROOT / p).read_text() for p in SOURCES]
    if mutation:
        name, index, func, old, new, expected = mutation
        start = sources[index].index(func + '(')
        before, after = sources[index][:start], sources[index][start:]
        assert old in after.split('\n}', 1)[0], name
        sources[index] = before + after.replace(old, new, 1)
    with tempfile.TemporaryDirectory(prefix='tvramfix-') as directory:
        work = pathlib.Path(directory)
        for inc, source in zip(('console_source.inc', 'fm_source.inc', 'db_source.inc', 'gfx_core_source.inc', 'gfx_vram_source.inc'), sources):
            (work / inc).write_text(source)
        exe = work / 'fixture'
        cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-ffreestanding',
               '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
               '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-D__KERNEL_BUILD__']
        cmd += ['-I' + str(work)]
        cmd += ['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'include',
            'arch/x86', 'platform/pc98', 'kernel', 'drivers', 'lib', 'sdk/include/os32', 'kapi', 'exec', 'fs', 'gfx', 'lib/sqlite3')]
        p = subprocess.run(cmd + [str(ROOT / 'tools/tests/kapi_bounds_host.c'), '-o', str(exe)],
                           capture_output=True, text=True)
        assert p.returncode == 0, p.stderr
        p = host32.run([str(exe)], runner=runner, capture_output=True, text=True, timeout=30)
        if mutation:
            assert p.returncode == 1 and 'FAIL ' + expected in p.stdout, (name, p.returncode, p.stdout)
            print('RED', name, expected)
        else:
            assert p.returncode == 0, (p.returncode, p.stdout, p.stderr)
            print(p.stdout.strip())

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    run(args.runner)
    if args.mutate:
        for mutation in MUTATIONS:
            run(args.runner, mutation)
        print(f'PASS {len(MUTATIONS)}/{len(MUTATIONS)} runtime mutations')
