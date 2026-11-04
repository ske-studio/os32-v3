"""OBS real-C scan/ownership tests, runtime mutants and normal code isolation."""
CHECK = dict(name='splash-obs-host', tier=1,
    covers=['drivers/video/pc98/splash_raster_image.c',
            'drivers/video/pc98/splash_raster.c',
            'drivers/video/pc98/splash_raster_image.h', 'drivers/video/pc98/splash_raster.h',
            'gfx/gfx_osri.c', 'gfx/gfx_osri.h', 'platform/pc98/splash_obs.c',
            'platform/pc98/splash.c', 'platform/pc98/splash_image.c', 'init/boot_splash.c',
            'include/platform_splash.h', 'tools/tests/splash_obs_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'lib/le_internal.h', 'lib/crc32_core.inc', 'lib/serial_diag.h', 'lib/kstring.h',
            'drivers/char/serial.h', 'include/config.h', 'gfx/palette.h'],
    needs=[], budget=60.0)

import argparse
import os
from pathlib import Path
import re
import subprocess
import struct
import zlib
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
NORMAL_BASE = '22685d9b'
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
    ('prepare capacity', PLAYER, 'image->count > OBS_V2_MAX_EVENTS', 'image->count >= OBS_V2_MAX_EVENTS'),
    ('row order', PLAYER, 'e->line != y || e->index', 'e->line > y || e->index'),
    ('47 bound', PLAYER, '> PL_OBS_OUT_LIMIT) return -1;', '> PL_OBS_OUT_LIMIT + 1) return -1;'),
    ('component G', PLAYER, 'if ((old & 0x0f0) != (rgb & 0x0f0)) {', 'if (1) {'),
    ('component R', PLAYER, 'if ((old & 0xf00) != (rgb & 0xf00)) {', 'if (1) {'),
    ('component B', PLAYER, 'if ((old & 0x00f) != (rgb & 0x00f)) {', 'if (1) {'),
    ('carry colors', PLAYER, 'current[e->index] = rgb;', '(void)rgb;'),
    ('max stats', PLAYER, 'plan->max_outs = r->count;', 'plan->max_outs = 0;'),
    ('total stats', PLAYER, 'plan->total_outs += r->count;', 'plan->total_outs = r->count;'),
    ('early selection', PLAYER, 'if (status & GDC_STAT_VSYNC) return PL_FRAME_DROP;\n        if (!(status & GDC_STAT_HBLANK)) {\n            if (!armed) {\n                armed = 1;\n                if (r->count) outp(PAL_IDX_PORT, r->first);', 'if (status & GDC_STAT_VSYNC) return PL_FRAME_DROP;\n        if (!(status & GDC_STAT_HBLANK)) {\n            if (!armed) {\n                armed = 1;\n                (void)r;'),
    ('duplicate OBS', 'gfx/gfx_osri.c', 'if (seen & (1u << ev->index)) goto done;', 'if (0 && (seen & (1u << ev->index))) goto done;'),
    ('6384 bound', 'gfx/gfx_osri.c', 'OSRI_V1_MAX_EVENTS : OBS_V2_MAX_EVENTS)',
     'OSRI_V1_MAX_EVENTS : OBS_V2_MAX_EVENTS - 1)'),
    ('next index', PLAYER, 'r->value[r->count++] = e->index;', 'r->value[r->count++] = 0;'),
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
    ('drop hides no_sync', OWNER, '!kstrcmp(status, "ok") && !frames && drops',
     '!frames && drops'),
    ('omit UART init', OWNER, '    serial_init(SYS_SERIAL_BAUD);', '    (void)SYS_SERIAL_BAUD;'),
    ('invisible label', OWNER, '#define SPLASH_OBS_LABEL_ATTR TATTR_WHITE',
     '#define SPLASH_OBS_LABEL_ATTR 0x07u'),
    ('preview shortened', PLAYER, 'round < rounds; round++)\n        if (preview_vsync',
     'round + 1u < rounds; round++)\n        if (preview_vsync'),
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


def asset_fixtures(work, directory):
    """Independent v2 decode to a serialized plan; no encoder imports/sidecar."""
    declarations, calls, assembly = [], [], ['.section .rodata']
    for n in range(1, 9):
        path = directory / f'N{n:02}.osri'
        data = path.read_bytes()
        checked = bytearray(data)
        checked[48:52] = bytes(4)
        assert zlib.crc32(checked) == struct.unpack_from('<I', data, 48)[0]
        assert struct.unpack_from('<H', data, 4)[0] == 2
        count = struct.unpack_from('<I', data, 36)[0]
        assert len(data) == 128096 + 8*count
        colors = list(struct.unpack_from('<16H', data, 64))
        rows = [[] for _ in range(400)]
        first = [0]*400
        seen, previous = set(), 0
        for j in range(count):
            y, index, reserved, color, reserved2 = struct.unpack_from('<HBBHH', data, 128096+8*j)
            assert 1 <= y < 400 and previous <= y and index < 16
            assert not reserved and not reserved2 and color <= 4095
            if y != previous:
                seen = set()
            assert index not in seen
            seen.add(index)
            previous = y
            old = colors[index]
            colors[index] = color
            components = [(port, (color >> shift) & 15) for port, shift in
                          ((0xaa, 4), (0xac, 8), (0xae, 0))
                          if (old >> shift) & 15 != (color >> shift) & 15]
            if components:
                if rows[y]:
                    rows[y].append((0xa8, index))
                else:
                    first[y] = index
                rows[y].extend(components)
            assert len(rows[y]) <= 47
        expected = bytearray()
        for y, row in enumerate(rows):
            expected += bytes([first[y], len(row)])
            expected += bytes(port for port, value in row) + bytes(64-len(row))
            expected += bytes(value for port, value in row) + bytes(64-len(row))
        maximum, total = max(map(len, rows)), sum(map(len, rows))
        expected += struct.pack('<II', maximum, total)
        fixture = work / f'asset{n}.bin'
        fixture.write_bytes(data + expected)
        assembly += [f'.global asset{n}', f'asset{n}: .incbin "{fixture}"']
        declarations.append(f'extern const u8 asset{n}[];')
        calls.append(f'check_asset(asset{n}, {len(data)});')
        print(f'N{n:02}: independent I/O max={maximum} total={total} events={count}', flush=True)
    helper = """
static void check_asset(const u8 *data, u32 size) {
    struct gfx_kernel_fb fb;
    scenario=2; file_bytes=size;
    for (u32 i=0; i<size; i++) file[i]=data[i];
    gfx_kernel_framebuffer(&fb);
    REQUIRE(!osri_load("asset", &fb, &image));
    REQUIRE(!pl_obs_prepare(&image, &plan));
    for (u32 i=0; i<sizeof(plan); i++) REQUIRE(((u8 *)&plan)[i] == data[size+i]);
}
"""
    source = work / 'tools/tests/splash_obs_host.c'
    source.write_text(source.read_text().replace('void _start(void)',
        '\n'.join(declarations) + helper + 'void _start(void)').replace(
        '    test_prepare();', '    ' + ' '.join(calls) + '\n    test_prepare();'))
    asm = work / 'assets.S'
    asm.write_text('\n'.join(assembly) + '\n.section .note.GNU-stack,"",@progbits\n')
    return [str(asm)]


def execute(mutation=None, asset_dir=None):
    with tempfile.TemporaryDirectory(prefix='splash-obs-') as td:
        work = Path(td)
        populate(work, mutation)
        assets = asset_fixtures(work, asset_dir) if asset_dir else []
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
                        *assets, '-lgcc', '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True)
        if result.returncode:
            print(result.stderr.decode(), end='', flush=True)
        return result.returncode


def normal_sections():
    """Compare normal code to the fixed pre-OBS base, including after commit.

    FIX1 intentionally adds the preview to R1. Only its new helper and its
    prepare/frame hooks may differ; every other normal section stays exact.
    Runtime tests separately exercise the preview and its replay hooks.
    """
    preview_sections = {'.text.pl_preview', '.text.preview_vsync',
                        '.text.preview_rounds', '.text.preview', '.text.pl_prepare', '.text.pl_frame'}
    with tempfile.TemporaryDirectory(prefix='splash-normal-') as td:
        work = Path(td)
        cc = compiler()
        objdump = str(Path(cc).with_name('i386-elf-objdump'))
        objcopy = str(Path(cc).with_name('i386-elf-objcopy'))
        for rel in [*NORMAL_UNITS, HEADER, 'include/platform_splash.h',
                    'drivers/video/pc98/splash_raster_image.h']:
            dest = work / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(subprocess.check_output(['git', 'show', NORMAL_BASE + ':' + rel], cwd=ROOT))
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
                    if rel != PLAYER or name not in preview_sections:
                        data[name] = blob.read_bytes()
                sections.append(data)
            assert sections[0] == sections[1], f'normal code changed: {rel}'
    print('GREEN normal .text/.rodata vs ' + NORMAL_BASE +
          ' (six units; explicit R1 preview hooks excluded)', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--obs-dir', type=Path, help='also compare N01..N08 to independent I/O decode')
    args = parser.parse_args()
    assert execute(asset_dir=args.obs_dir) == 0, 'OBS baseline runtime failed'
    print('GREEN variable OUTs, pi, 47/48 boundary, 6384 capacity, exact columns, both modes/phases, IF, '
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
