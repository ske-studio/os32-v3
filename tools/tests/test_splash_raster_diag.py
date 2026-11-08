"""Diagnostic records execute; the ordinary player retains identical text."""
CHECK = dict(name='splash-raster-diag-host', tier=1,
    covers=['platform/pc98/bootinfo_wire.h', 'platform/pc98/bootinfo_native.h', 'drivers/video/pc98/splash_raster.c', 'drivers/video/pc98/splash_raster_image.c', 'drivers/video/pc98/splash_raster.h',
            'tools/tests/splash_raster_diag_host.c', 'tools/tests/splash_raster_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'gfx/gfx_osri.h', 'gfx/gfx.h', 'include/types.h', 'include/pc98.h',
            'include/pegc.h', 'include/sys.h', 'gfx/palette.h',
            'sdk/include/os32/os32_kapi_shared.h', 'platform/pc98/splash_probe.c',
            'tools/tests/splash_probe_host.c', 'tools/tests/splash_raster_model_host.c',
            'drivers/video/pc98/splash_raster_image.h', 'include/platform_splash.h',
            'gfx/gfx_internal.h', 'include/gfx_hal.h', 'mm/paging.h',
            'platform/pc98/legacy/v86_bios.h', 'drivers/char/serial.h', 'lib/serial_diag.h',
            'include/bootinfo.h', 'include/build_id.h', 'include/config.h',
            'arch/x86/kernel/idt.h', 'kernel/time/ktime.h'], needs=[], budget=20.0)
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'drivers/video/pc98/splash_raster.c'
MUTATIONS = (
    ('omit interval total', 'diag.if0_total_us += span;', '(void)span;'),
    ('omit maximum', 'diag.if0_max_us = span;', 'diag.if0_max_us = 0;'),
    ('lose target', 'diag_events[i].target_us = target;', 'diag_events[i].target_us = 0; (void)target;'),
    ('lose end', 'diag_events[i].end_us = diag_now();', 'diag_events[i].end_us = 0;'),
    ('lose late counter', 'diag.late = s->late;', 'diag.late = 0;'),
    ('lose frame reset', 'DIAG(diag_reset());\n#ifdef SPLASH_RASTER_DIAG', 'DIAG((void)0);\n#ifdef SPLASH_RASTER_DIAG'),
)
def execute(source):
    with tempfile.TemporaryDirectory(prefix='splash-diag-') as td:
        work = Path(td)
        for rel in (SOURCE, 'tools/tests/splash_raster_host.c', 'tools/tests/splash_raster_diag_host.c'):
            dest = work/rel; dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(source) if rel == SOURCE else shutil.copyfile(ROOT/rel, dest)
        cc = str(Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc')
        exe = work/'diag'
        subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
            '-D__KERNEL_BUILD__', '-DSPLASH_RASTER_DIAG', *include_flags(ROOT),
            '-I'+str(ROOT/'drivers/video/pc98'), str(work/'tools/tests/splash_raster_diag_host.c'),
            '-lgcc', '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True)
        if result.returncode: print(result.stderr, flush=True)
        return result.returncode


PROBE = 'platform/pc98/splash_probe.c'
CAPTURE = 'u32 count = ((PROBE_HPERIODS + 1) * s->line_num_us + q + probe_q_lower - 1) / probe_q_lower + 1;'
def model(truncate=False):
    with tempfile.TemporaryDirectory(prefix='splash-model-') as td:
        work = Path(td)
        for rel in (PROBE, SOURCE, 'tools/tests/splash_probe_host.c',
                    'tools/tests/splash_raster_model_host.c'):
            dest = work/rel; dest.parent.mkdir(parents=True, exist_ok=True)
            source = (ROOT/rel).read_text()
            if rel == PROBE and truncate:
                assert source.count(CAPTURE) == 1
                source = source.replace(CAPTURE, 'u32 count = s->line_num_us / q;', 1)
            dest.write_text(source)
        cc = str(Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc'); exe = work/'model'
        subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
            '-D__KERNEL_BUILD__', '-DSPLASH_RASTER_DIAG', *include_flags(ROOT),
            '-I'+str(ROOT/'drivers/video/pc98'), str(work/'tools/tests/splash_raster_model_host.c'),
            '-lgcc', '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True)
        if result.returncode: print(result.stderr, flush=True)
        return result.returncode

def ordinary_symbols():
    with tempfile.TemporaryDirectory(prefix='splash-ordinary-') as td:
        obj = Path(td)/'raster.o'
        prefix = Path(os.environ['CROSS_DIR'])/'bin/i386-elf-'
        subprocess.run([str(prefix)+'gcc', '-std=gnu11', '-O2', '-ffreestanding',
            '-D__KERNEL_BUILD__', *include_flags(ROOT), '-I'+str(ROOT/'drivers/video/pc98'),
            '-c', str(ROOT/SOURCE), '-o', str(obj)], check=True)
        symbols = subprocess.check_output([str(prefix)+'nm', '-a', str(obj)], text=True)
        assert 'diag' not in symbols.lower()
        sections = subprocess.check_output([str(prefix)+'size', '-A', str(obj)], text=True)
        assert any(line.split()[:2] == ['.bss', '60'] for line in sections.splitlines())
    print('GREEN ordinary build contains no diagnostic code/storage', flush=True)

def main():
    p = argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true'); args = p.parse_args()
    source = (ROOT/SOURCE).read_text()
    ordinary_symbols()
    assert model() == 0
    print('GREEN independent waveform: 318 phases, 64 complete periods', flush=True)
    assert execute(source) == 0
    print('GREEN diagnostic targets/start/end, IF maximum/total, late, reset, fixed palette', flush=True)
    if args.mutate:
        rc = model(truncate=True)
        assert rc != 0
        print(f'RED truncate continuous H capture: compiled, runtime rc={rc}', flush=True)
        for name, old, new in MUTATIONS:
            assert source.count(old) == 1, name
            rc = execute(source.replace(old, new, 1))
            assert rc != 0, name
            print(f'RED {name}: compiled, runtime rc={rc}', flush=True)
def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_splash_raster_diag.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
if __name__ == '__main__': main()
