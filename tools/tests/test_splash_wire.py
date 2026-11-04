"""Boot wire: fixed/v1/v2 OSRI, real loader/players, deadlines and cleanup."""
CHECK = dict(name='splash-wire-host', tier=1,
    covers=['init/boot_splash.c', 'drivers/video/pc98/splash_raster_image.c', 'platform/pc98/splash.c', 'platform/pc98/splash_image.c',
            'drivers/video/pc98/splash_raster_plan.c', 'drivers/video/pc98/splash_raster_play.c',
            'drivers/video/pc98/splash_raster_play.h', 'drivers/video/pc98/splash_raster_image.h',
            'gfx/osri_profile.h', 'include/memmap.h', 'include/platform_splash.h', 'include/config.h', 'drivers/video/pc98/splash_raster.c',
            'drivers/video/pc98/splash_raster.h', 'gfx/gfx_osri.c', 'gfx/gfx_osri.h',
            'tools/tests/splash_wire_host.c', 'tools/tests/splash_raster_host.c',
            'tools/tests/kernel_includes.py', 'tools/tests/host32.py', 'lib/le_internal.h',
            'lib/crc32_core.inc', 'lib/kstring.h', 'mm/kmalloc.h', 'fs/vfs.h',
            'gfx/gfx.h', 'gfx/palette.h', 'include/types.h', 'include/pc98.h', 'include/pegc.h',
            'include/sys.h', 'include/gfx_hal.h', 'include/tvram.h',
            'sdk/include/os32/os32_kapi_shared.h'], needs=[], budget=30.0)
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['init/boot_splash.c', 'platform/pc98/splash.c', 'platform/pc98/splash_image.c',
           'drivers/video/pc98/splash_raster.c', 'drivers/video/pc98/splash_raster_plan.c',
           'drivers/video/pc98/splash_raster_play.c', 'drivers/video/pc98/splash_raster_image.c',
           'gfx/gfx_osri.c', 'lib/le_internal.h',
           'lib/crc32_core.inc', 'tools/tests/splash_wire_host.c', 'tools/tests/splash_raster_host.c']
MUTATIONS = (
    ('omit release', 'platform/pc98/splash_image.c', 'if (image) kfree(image);', '(void)image;'),
    ('omit close', 'gfx/gfx_osri.c', 'vfs_close(fd);', '(void)fd;'),
    ('omit fixed commit', 'platform/pc98/splash_image.c', '    gfx_present_nosync();', '    (void)fb;'),
    ('extend deadline', 'init/boot_splash.c', '    /* パレット設定 */', '    end_tick = tick_count + PLATFORM_SPLASH_TICKS;\n    /* パレット設定 */'),
    ('omit static pacing', 'init/boot_splash.c', 'if (platform_splash_wait_frame(end_tick)) break;', 'break;'),
    ('omit failed-load redraw', 'init/boot_splash.c', 'platform_splash_clear(PAL_GRAD);', '(void)PAL_GRAD;'),
    ('ignore CRC', 'gfx/gfx_osri.c', 'crc32_core_final(crc) != expected', '(0 && crc32_core_final(crc) != expected)'),
)
def osri(events=False):
    ev = struct.pack('<HBBHH', 4, 1, 0, 0x345, 0) if events else b''
    if events == 2:
        ev = b''.join(struct.pack('<HBBHH', 1, i, 0, 0x11 if i == 15 else 1, 0)
                      for i in range(16))
    elif events == 3:
        # Keep the wire's 1us/OUT clock inside one line. The owner test
        # independently exercises the 31-OUT acceptance boundary.
        ev = struct.pack('<HBBHH', 1, 1, 0, 0x345, 0)
    header = struct.pack('<4sHHHHBBHIIIIIIHHHHIIII', b'OSRI', 2 if events in (2, 3) else 1, 64, 640, 400,
        4, 1, 0, 128096 + len(ev), 64, 96, 128000, 128096, len(ev)//8, 8, 0, 1 if events in (2, 3) else 4, 0, 0, 0, 0, 0)
    data = bytearray(header + struct.pack('<16H', *([0]*16 if events in (2, 3) else range(16))) +
        b''.join(bytes([x])*32000 for x in (0x81, 0x42, 0x24, 0x18)) + ev)
    struct.pack_into('<I', data, 48, zlib.crc32(data))
    return data

def execute(mutation=None):
    with tempfile.TemporaryDirectory(prefix='splash-wire-') as td:
        work = Path(td)
        for rel in SOURCES:
            dest = work/rel; dest.parent.mkdir(parents=True, exist_ok=True)
            source = (ROOT/rel).read_text()
            if mutation and mutation[1] == rel:
                _, _, old, new = mutation
                assert source.count(old) == 1, mutation[0]
                source = source.replace(old, new, 1)
            if rel == 'tools/tests/splash_raster_host.c':
                source = source.replace('#include "types.h"', '#include "types.h"\nextern volatile u32 tick_count;')
                source = source.replace('if (!frozen) clock_us++;', 'if (!frozen) clock_us++;\n    tick_count = clock_us / 10000;')
                source = source.replace('static unsigned int inp(unsigned int port)\n{', 'static unsigned int inp(unsigned int port)\n{\n    advance();')
                source = source.replace('#define HOST_WRITES 4096u', '#define HOST_WRITES 65536u')
            dest.write_text(source)
        (work/'wire_data.h').write_text('\n'.join(
            'static const unsigned char wire_'+name+'[] = {' + ','.join(map(str, osri(ev))) + '};'
            for name, ev in [('fixed', False), ('events', True), ('budget', 2), ('dynamic', 3)]))
        cc = str(Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc'); exe = work/'wire'
        subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-D__KERNEL_BUILD__',
            '-I'+str(work), *include_flags(ROOT), '-I'+str(ROOT/'drivers/video/pc98'),
            str(work/'tools/tests/splash_wire_host.c'), '-lgcc', '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True)
        if result.returncode: print('runtime rc=', result.returncode, result.stderr, flush=True)
        return result.returncode

def main():
    p = argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true'); args = p.parse_args()
    assert execute() == 0
    print('GREEN generated fixed/v1/v2 OSRI, 32 OUT fallback, short reads/CRC/EOF, ownership, shared deadline, IF and cleanup', flush=True)
    if args.mutate:
        for mutation in MUTATIONS:
            rc = execute(mutation)
            assert rc != 0, mutation[0]
            print(f'RED {mutation[0]}: compiled, runtime rc={rc}', flush=True)
        assert execute() == 0
        print('GREEN restored boot wire', flush=True)
def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_splash_wire.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
if __name__ == '__main__': main()
