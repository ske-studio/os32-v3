"""Run the diagnostic probe with bounded fake scan/clock and buffered output."""
CHECK = dict(name='splash-probe-host', tier=1,
    covers=['platform/pc98/splash_probe.c', 'drivers/video/pc98/splash_raster.c',
            'drivers/video/pc98/splash_raster.h', 'tools/tests/splash_probe_host.c',
            'tools/tests/splash_raster_host.c', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'include/platform_splash.h',
            'gfx/gfx_internal.h', 'gfx/gfx_osri.h', 'gfx/gfx.h', 'gfx/palette.h', 'include/gfx_hal.h',
            'include/pc98.h', 'include/pegc.h', 'include/types.h', 'include/sys.h',
            'mm/paging.h', 'drivers/char/serial.h', 'lib/serial_diag.h',
            'include/bootinfo.h', 'include/build_id.h', 'arch/x86/kernel/idt.h',
            'kernel/time/ktime.h', 'sdk/include/os32/os32_kapi_shared.h'], needs=[], budget=20.0)
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'platform/pc98/splash_probe.c'
MUTATIONS = (
    ('omit pattern', '    probe_pattern();', '    (void)probe_pattern;'),
    ('omit receive', '        probe_receive();', '        (void)probe_receive;'),
    ('omit frame duration', '.duration = probe_now() - stamp,', '.duration = 0,'),
    ('print during scan', '    probe_pattern();', '    serial_puts_polled("early");\n    probe_pattern();'),
)
def execute(mutation=None):
    with tempfile.TemporaryDirectory(prefix='splash-probe-') as td:
        work = Path(td)
        for rel in (SOURCE, 'drivers/video/pc98/splash_raster.c', 'tools/tests/splash_probe_host.c', 'tools/tests/splash_raster_host.c'):
            dest = work/rel; dest.parent.mkdir(parents=True, exist_ok=True)
            source = (ROOT/rel).read_text()
            if rel == SOURCE and mutation:
                name, old, new = mutation
                assert source.count(old) == 1, name
                source = source.replace(old, new, 1)
            if rel == 'tools/tests/splash_raster_host.c':
                source = source.replace('REQUIRE(port == GDC_GFX_STAT);', 'REQUIRE(port == GDC_GFX_STAT || port == GDC_TEXT_STAT);')
                source = source.replace('REQUIRE(writes < 1024);', 'writes %= 1024;')
            dest.write_text(source)
        cc = str(Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc'); exe = work/'probe'
        subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
            '-D__KERNEL_BUILD__', '-DSPLASH_RASTER_DIAG', *include_flags(ROOT),
            '-I'+str(ROOT/'drivers/video/pc98'), str(work/'tools/tests/splash_probe_host.c'),
            '-lgcc', '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True)
        if result.returncode: print('runtime rc=', result.returncode, result.stderr, flush=True)
        return result.returncode

def main():
    p = argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true'); args = p.parse_args()
    assert execute() == 0
    print('GREEN probe both modes, 32 VS/64 H periods, pattern/ruler, serial sequence, clock/IO, buffered output, bounded failure', flush=True)
    if args.mutate:
        for mutation in MUTATIONS:
            rc = execute(mutation)
            assert rc != 0, mutation[0]
            print(f'RED {mutation[0]}: compiled, runtime rc={rc}', flush=True)
def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_splash_probe.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
if __name__ == '__main__': main()
