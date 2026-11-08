"""OBS real-C scan/ownership tests, runtime mutants and shared normal/OBS code."""
CHECK = dict(name='splash-obs-host', tier=1,
    covers=['drivers/video/pc98/splash_raster_image.c',
            'drivers/video/pc98/splash_raster_plan.c', 'drivers/video/pc98/splash_raster_play.c',
            'drivers/video/pc98/splash_raster_play.h', 'gfx/osri_profile.h',
            'drivers/video/pc98/splash_raster.c',
            'drivers/video/pc98/splash_raster_image.h', 'drivers/video/pc98/splash_raster.h',
            'gfx/gfx_osri.c', 'gfx/gfx_osri.h', 'platform/pc98/splash_obs.c',
            'platform/pc98/splash.c', 'platform/pc98/splash_image.c', 'init/boot_splash.c',
            'include/platform_splash.h', 'tools/tests/splash_obs_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'lib/le_internal.h', 'lib/crc32_core.inc', 'lib/serial_diag.h', 'lib/kstring.h',
            'drivers/char/serial.h', 'include/config.h', 'gfx/palette.h'],
    needs=[], budget=9.2)

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
PLAYER = 'drivers/video/pc98/splash_raster_play.c'
PLAN = 'drivers/video/pc98/splash_raster_plan.c'
PREVIEW = 'drivers/video/pc98/splash_raster_image.c'
OWNER = 'platform/pc98/splash_obs.c'
HEADER = 'gfx/gfx_osri.h'
SOURCES = [PLAYER, PLAN, PREVIEW, 'gfx/osri_profile.h',
           'drivers/video/pc98/splash_raster_play.h', OWNER, HEADER, 'gfx/gfx_osri.c',
           'drivers/video/pc98/splash_raster_image.h', 'include/platform_splash.h',
           'init/boot_splash.c', 'lib/le_internal.h', 'lib/crc32_core.inc',
           'tools/tests/splash_obs_host.c']
MUTATIONS = (
    ('prepare capacity', PLAN, 'image->count > OSRI_IMAGE_EVENTS', 'image->count >= OSRI_IMAGE_EVENTS'),
    ('row order', PLAN, 'e->line != y || e->line % image->band', 'e->line > y || e->line % image->band'),
    ('31 bound', PLAN, 'r->count + outs + !!r->count > OSRI_DEFAULT_OUT_BUDGET ||\n                r->count + outs + !!r->count > sizeof(r->port)', 'r->count + outs + !!r->count > 47u'),
    ('check too late', PLAN, 'r->count + outs + !!r->count > OSRI_DEFAULT_OUT_BUDGET ||\n                r->count + outs + !!r->count > sizeof(r->port)', 'r->count + 0u * outs > OSRI_DEFAULT_OUT_BUDGET'),
    ('31 equality', PLAN, '> OSRI_DEFAULT_OUT_BUDGET ||', '>= OSRI_DEFAULT_OUT_BUDGET ||'),
    ('component G', PLAN, 'if ((old & 0x0f0) != (rgb & 0x0f0)) {', 'if (1) {'),
    ('component R', PLAN, 'if ((old & 0xf00) != (rgb & 0xf00)) {', 'if (1) {'),
    ('component B', PLAN, 'if ((old & 0x00f) != (rgb & 0x00f)) {', 'if (1) {'),
    ('carry colors', PLAN, 'current[e->index] = rgb;', '(void)rgb;'),
    ('max stats', PLAN, 'plan->max_outs = r->count;', 'plan->max_outs = 0;'),
    ('total stats', PLAN, 'plan->total_outs += r->count;', 'plan->total_outs = r->count;'),
    ('early selection', PLAYER, 'if (r->count) outp(PAL_IDX_PORT, r->first);', '(void)r;'),
    ('duplicate', 'gfx/gfx_osri.c', 'if (seen & (1u << ev->index)) goto done;', 'if (0 && (seen & (1u << ev->index))) goto done;'),
    ('6384 bound', 'gfx/gfx_osri.c', 'OSRI_V1_MAX_EVENTS : OSRI_V2_MAX_EVENTS)', 'OSRI_V1_MAX_EVENTS : OSRI_V2_MAX_EVENTS - 1)'),
    ('next index', PLAN, 'r->value[r->count++] = e->index;', 'r->value[r->count++] = 0;'),
    ('low rearm', PLAYER, 'else if (armed) {\n            emit(r);', 'else {\n            emit(r);'),
    ('missing CLI', PLAYER, 'unsigned int flags = irq_save();', 'unsigned int flags = 1; (void)irq_save;'),
    ('missing restore', PLAYER, 'play_done:\n    irq_restore(flags);', 'play_done:\n    (void)flags; (void)irq_restore;'),
    ('VSYNC drop', PLAYER, 'if (status & GDC_STAT_VSYNC) return PL_FRAME_DROP;', 'if (0 && (status & GDC_STAT_VSYNC)) return PL_FRAME_DROP;'),
    ('double VBLANK', PLAYER, 'if (play_vsync(0) || play_vsync(1)) return PL_FRAME_NO_SYNC;', 'if (play_vsync(0) || play_vsync(1) || play_vsync(0) || play_vsync(1)) return PL_FRAME_NO_SYNC;'),
    ('finite wait', PLAYER, 'i < PL_PLAY_POLL_LIMIT; i++)\n        if (!!', '1; i++)\n        if (!!'),
    ('omit plan release', OWNER, 'if (plan) kfree(plan);', '(void)plan;'),
    ('omit image release', OWNER, 'if (image) kfree(image);', '(void)image;'),
    ('drop extends duration', OWNER, 'round < rounds; round++', 'frames < rounds; round++'),
    ('duration rounding', OWNER, '(duration + period - 1) / period', 'duration / period'),
    ('drop hides no_sync', OWNER, '!kstrcmp(status, "ok") && !frames && drops', '!frames && drops'),
    ('omit UART init', OWNER, '    serial_init(SYS_SERIAL_BAUD);', '    (void)SYS_SERIAL_BAUD;'),
    ('invisible label', OWNER, '#define SPLASH_OBS_LABEL_ATTR TATTR_WHITE', '#define SPLASH_OBS_LABEL_ATTR 0x07u'),
    ('preview shortened', PREVIEW, 'round < rounds; round++)\n        if (preview_vsync', 'round + 1u < rounds; round++)\n        if (preview_vsync'),
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
            assert len(rows[y]) <= 31
        expected = bytearray()
        for y, row in enumerate(rows):
            expected += bytes([first[y], len(row)])
            expected += bytes(port for port, value in row) + bytes(31-len(row))
            expected += bytes(value for port, value in row) + bytes(31-len(row))
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
    REQUIRE(!pl_v2_prepare(&image, &plan));
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
            'void _start(void) { int rc = sizeof(struct OsriImage) != 51110;'
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
    """Both builds compile exactly the same preparation and scan columns."""
    with tempfile.TemporaryDirectory(prefix='splash-shared-') as td:
        work = Path(td)
        cc = compiler()
        objcopy = str(Path(cc).with_name('i386-elf-objcopy'))
        nm = str(Path(cc).with_name('i386-elf-nm'))
        for rel in (PLAYER, PLAN):
            blobs = []
            for obs in (False, True):
                obj = work / ('obs.o' if obs else 'normal.o')
                subprocess.run([cc, *flags(ROOT), *(['-DSPLASH_RASTER_OBS'] if obs else []),
                                '-c', str(ROOT / rel), '-o', str(obj)], check=True)
                symbols = subprocess.check_output([nm, '-a', str(obj)], text=True)
                assert not re.search(r'pl_emit(?:3[2-9]|4[0-7])\b|__(?:u?div|u?mod)di3', symbols)
                blob = work / 'column.bin'
                subprocess.run([objcopy, '-O', 'binary', str(obj), str(blob)], check=True)
                blobs.append(blob.read_bytes())
            assert blobs[0] == blobs[1], rel
        obj = work / 'preview.o'
        subprocess.run([cc, *flags(ROOT), '-c', str(ROOT / PREVIEW), '-o', str(obj)], check=True)
        assert not re.search(r'__(?:u?div|u?mod)di3', subprocess.check_output([nm, '-u', str(obj)], text=True))
    print('GREEN normal/OBS shared columns; emit 32..47 and 64-bit division helpers absent', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--obs-dir', type=Path, help='also compare N01..N08 to independent I/O decode')
    args = parser.parse_args()
    assert execute(asset_dir=args.obs_dir) == 0, 'OBS baseline runtime failed'
    print('GREEN variable OUTs, pi, 31/32 boundary, 6384 capacity, exact columns, both modes/phases, IF, '
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
