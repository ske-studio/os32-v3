"""OBS real-C scan/ownership tests, runtime mutants and normal code isolation."""
CHECK = dict(name='splash-obs-host', tier=1,
    covers=['drivers/video/pc98/splash_raster_image.c',
            'drivers/video/pc98/splash_raster.c',
            'drivers/video/pc98/splash_raster_image.h', 'drivers/video/pc98/splash_raster.h',
            'gfx/gfx_osri.c', 'gfx/gfx_osri.h', 'platform/pc98/splash_obs.c',
            'platform/pc98/splash.c', 'platform/pc98/splash_image.c', 'init/boot_splash.c',
            'include/platform_splash.h', 'tools/tests/splash_obs_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'lib/le_internal.h', 'lib/crc32_core.inc', 'lib/serial_diag.h'],
    needs=[], budget=60.0)

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PLAYER = 'drivers/video/pc98/splash_raster_image.c'
OWNER = 'platform/pc98/splash_obs.c'
HEADER = 'gfx/gfx_osri.h'
SOURCES = [PLAYER, OWNER, HEADER, 'gfx/gfx_osri.c',
           'drivers/video/pc98/splash_raster_image.h', 'include/platform_splash.h',
           'init/boot_splash.c', 'lib/le_internal.h', 'lib/crc32_core.inc',
           'tools/tests/splash_obs_host.c']
NORMAL_UNITS = ['gfx/gfx_osri.c', PLAYER, 'init/boot_splash.c',
                'drivers/video/pc98/splash_raster.c', 'platform/pc98/splash.c',
                'platform/pc98/splash_image.c']
MUTATIONS = (
    ('N minimum', PLAYER, 'colors < 1 || colors > OSRI_COLORS', 'colors < 2 || colors > OSRI_COLORS'),
    ('N maximum', PLAYER, 'colors > OSRI_COLORS ||', 'colors >= OSRI_COLORS ||'),
    ('row equality', PLAYER,
     'image->count != (SPLASH_RASTER_VISIBLE_LINES - 1) * colors',
     'image->count < (SPLASH_RASTER_VISIBLE_LINES - 1) * colors'),
    ('missing row', PLAYER, 'e->line != y ||', 'e->line + 1u < y ||'),
    ('6384 bound', 'gfx/gfx_osri.c', 'OSRI_V1_MAX_EVENTS : OBS_V2_MAX_EVENTS)',
     'OSRI_V1_MAX_EVENTS : OBS_V2_MAX_EVENTS - 1)'),
    ('next index', PLAYER, 'r->value[r->count++] = e->index;', 'r->value[r->count++] = 0;'),
    ('same RGB omitted', PLAYER, 'r->port[r->count] = PAL_G_PORT;',
     'if (e->rgb444 == image->pal[e->index]) continue;\n            r->port[r->count] = PAL_G_PORT;'),
    ('low rearm', PLAYER, 'else if (armed) {\n            emit(r);',
     'else {\n            emit(r);'),
    ('missing CLI', PLAYER, 'unsigned int flags = irq_save();\n    int rc = PL_FRAME_DROP;',
     'unsigned int flags = 1;\n    int rc = PL_FRAME_DROP;'),
    ('visible IF open', PLAYER, 'int rc = PL_FRAME_DROP;\n    /* Protect F',
     'int rc = PL_FRAME_DROP;\n    irq_restore(flags);\n    /* Protect F'),
    ('missing restore', PLAYER, 'obs_done:\n    irq_restore(flags);', 'obs_done:\n    (void)flags;'),
    ('B1 VSYNC drop', PLAYER, 'if (status & GDC_STAT_VSYNC) return PL_FRAME_DROP;',
     'if (0 && (status & GDC_STAT_VSYNC)) return PL_FRAME_DROP;'),
    ('double VBLANK', PLAYER, 'if (obs_vsync(0) || obs_vsync(1)) return PL_FRAME_NO_SYNC;',
     'if (obs_vsync(0) || obs_vsync(1) || obs_vsync(0) || obs_vsync(1)) return PL_FRAME_NO_SYNC;'),
    ('finite wait', PLAYER, 'i < PL_OBS_POLL_LIMIT; i++)\n        if (!!',
     '1; i++)\n        if (!!'),
    ('omit plan release', OWNER, 'if (plan) kfree(plan);', '(void)plan;'),
    ('omit image release', OWNER, 'if (image) kfree(image);', '(void)image;'),
    ('drop extends duration', OWNER, 'round < rounds; round++', 'frames < rounds; round++'),
    ('duration rounding', OWNER, '(duration + period - 1) / period', 'duration / period'),
    ('OBS guard removed', HEADER,
     '#ifdef SPLASH_RASTER_OBS\n#define OBS_V2_MAX_EVENTS_PER_LINE',
     '#define SPLASH_RASTER_OBS 1\n#ifdef SPLASH_RASTER_OBS\n#define OBS_V2_MAX_EVENTS_PER_LINE'),
)


def compiler():
    return str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')


def flags(work):
    return ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffreestanding',
            '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
            '-D__KERNEL_BUILD__', '-I' + str(work / 'gfx'),
            '-I' + str(work / 'drivers/video/pc98'), '-I' + str(work / 'include'),
            *include_flags(ROOT)]


def populate(work, mutation=None):
    for rel in SOURCES:
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        text = (ROOT / rel).read_text()
        if mutation and mutation[1] == rel:
            name, _, old, new = mutation
            assert text.count(old) == 1, f'ambiguous/missing anchor: {name}'
            text = text.replace(old, new, 1)
        dest.write_text(text)


def execute(mutation=None):
    with tempfile.TemporaryDirectory(prefix='splash-obs-') as td:
        work = Path(td)
        populate(work, mutation)
        normal = work / 'normal.c'
        normal.write_text('#include "gfx_osri.h"\n'
            'void _start(void) { int rc = sizeof(struct OsriImage) != 9614;'
            '__asm__ volatile("int $0x80" : : "a"(1), "b"(rc));'
            '__builtin_unreachable(); }\n')
        exe = work / 'host'
        subprocess.run([compiler(), *flags(work), '-nostdlib',
                        '-Wl,--gc-sections', str(normal), '-o', str(exe)], check=True)
        rc = host32.run([str(exe)], runner='qemu', capture_output=True).returncode
        if rc:
            return rc
        subprocess.run([compiler(), *flags(work), '-nostdlib', '-DSPLASH_RASTER_OBS',
                        '-Wl,--gc-sections', str(work / 'tools/tests/splash_obs_host.c'),
                        '-lgcc', '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True)
        if result.returncode:
            print(result.stderr.decode(), end='', flush=True)
        return result.returncode


def normal_sections():
    """Compare all normal .text/.rodata sections with the worktree base (HEAD)."""
    with tempfile.TemporaryDirectory(prefix='splash-normal-') as td:
        work = Path(td)
        cc = compiler()
        objdump = str(Path(cc).with_name('i386-elf-objdump'))
        objcopy = str(Path(cc).with_name('i386-elf-objcopy'))
        for rel in [*NORMAL_UNITS, HEADER, 'include/platform_splash.h',
                    'drivers/video/pc98/splash_raster_image.h']:
            dest = work / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(subprocess.check_output(['git', 'show', 'HEAD:' + rel], cwd=ROOT))
        for rel in NORMAL_UNITS:
            sections = []
            for index, source in enumerate((work / rel, ROOT / rel)):
                obj = work / f'unit{index}.o'
                subprocess.run([cc, *flags(work if index == 0 else ROOT),
                                '-c', str(source), '-o', str(obj)], check=True)
                listing = subprocess.check_output([objdump, '-h', str(obj)], text=True)
                names = re.findall(r'^\s*\d+\s+(\.(?:text|rodata)[\w.]*)\s', listing, re.M)
                data = {}
                for name in names:
                    blob = work / 'section.bin'
                    subprocess.run([objcopy, '-O', 'binary', '--only-section=' + name,
                                    str(obj), str(blob)], check=True)
                    data[name] = blob.read_bytes()
                sections.append(data)
            assert sections[0] == sections[1], f'normal code changed: {rel}'
    print('GREEN normal .text/.rodata unchanged (six translation units)', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    assert execute() == 0, 'OBS baseline runtime failed'
    print('GREEN N=1..16, 6384 boundary, exact columns, both modes/phases, IF, '
          'VSYNC drop, finite waits, next frame, ownership, serial pairs and cleanup', flush=True)
    normal_sections()
    if args.mutate:
        for mutation in MUTATIONS:
            rc = execute(mutation)
            assert rc != 0, 'survived: ' + mutation[0]
            print(f'RED {mutation[0]}: compiled, runtime rc={rc}', flush=True)


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_splash_obs.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
