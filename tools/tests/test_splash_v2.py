"""Normal image owner: real preparation, bounded deadlines and cleanup."""
CHECK = dict(name='splash-v2-host', tier=1,
    covers=['platform/pc98/splash_image.c', 'include/platform_splash.h', 'init/boot_splash.c',
            'drivers/video/pc98/splash_raster_plan.c', 'drivers/video/pc98/splash_raster_play.h',
            'drivers/video/pc98/splash_raster_image.h', 'drivers/video/pc98/splash_raster.h',
            'gfx/gfx_osri.h', 'gfx/osri_profile.h', 'gfx/gfx.h', 'gfx/palette.h',
            'include/memmap.h', 'include/pegc.h', 'include/pc98.h', 'include/types.h',
            'include/io.h', 'include/gfx_hal.h', 'include/config.h',
            'sdk/include/os32/os32_kapi_shared.h', 'fs/vfs.h', 'mm/kmalloc.h', 'include/kprintf.h', 'lib/kstring.h',
            'tools/tests/splash_v2_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=30.0)
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
OWNER = 'platform/pc98/splash_image.c'
SOURCES = [OWNER, 'tools/tests/splash_v2_host.c', 'drivers/video/pc98/splash_raster_plan.c']
MUTATIONS = (
    ('reject dynamic', 'if (image->count) {', 'if (image->count) goto done;\n    if (image->count) {'),
    ('omit plan free', 'if (plan) kfree(plan);', '(void)plan;'),
    ('omit image free', 'if (image) kfree(image);', '(void)image;'),
    ('omit prepare', 'if (pl_v2_prepare(image, plan)) goto done;', '(void)pl_v2_prepare;'),
    ('omit preview', 'if (pl_preview(s)) goto done;', '(void)pl_preview;'),
    ('preview shortage', 'SPLASH_RASTER_PREVIEW_MS * SPLASH_RASTER_US_PER_MS) goto done;', '1u) goto done;'),
    ('omit preview charge', 'u32 prefix = play_start - start;', 'u32 prefix = 0;'),
    ('omit drop charge', 'consumed += period;', 'if (frame == PL_FRAME_OK) consumed += period;'),
    ('omit elapsed ticks', 'unsigned long long used = elapsed > consumed ? elapsed : consumed;', 'unsigned long long used = consumed; (void)elapsed;'),
    ('double charge', 'unsigned long long used = elapsed > consumed ? elapsed : consumed;', 'unsigned long long used = elapsed + consumed;'),
    ('all drop succeeds', 'if (good || !frames) rc = PLATFORM_SPLASH_DONE;', 'rc = PLATFORM_SPLASH_DONE; (void)good;'),
    ('ignore no sync', 'if (frame == PL_FRAME_NO_SYNC) goto play_done;', 'if (0 && frame == PL_FRAME_NO_SYNC) goto play_done;'),
    ('restart deadline', '*end_tick = now + rest;', '*end_tick = now + PLATFORM_SPLASH_TICKS; (void)rest;'),
    ('omit model residual', 'if (model > elapsed) elapsed = model;', '(void)model;'),
    ('round residual up', '(rest + 1u) *', '(rest + 0u) *'),
    ('omit black cleanup', 'if (transferred) image_black();', '(void)transferred;'),
)
def compiler():
    return str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
def flags(work):
    return ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffreestanding',
            '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__',
            '-I'+str(work/'drivers/video/pc98'), *include_flags(ROOT),
            '-I'+str(ROOT/'drivers/video/pc98')]
def execute(mutation=None):
    with tempfile.TemporaryDirectory(prefix='splash-v2-') as td:
        work=Path(td)
        for rel in SOURCES:
            dest=work/rel; dest.parent.mkdir(parents=True, exist_ok=True)
            text=(ROOT/rel).read_text()
            if mutation and rel == OWNER:
                name, old, new=mutation
                assert text.count(old) == 1, name
                text=text.replace(old,new,1)
            dest.write_text(text)
        exe=work/'owner'
        subprocess.run([compiler(), *flags(work), '-nostdlib',
            str(work/'tools/tests/splash_v2_host.c'), '-lgcc', '-o', str(exe)], check=True)
        result=host32.run([str(exe)], runner='qemu', capture_output=True)
        if result.returncode: print(result.stderr.decode(), end='', flush=True)
        return result.returncode

def no_division():
    with tempfile.TemporaryDirectory(prefix='splash-v2-symbols-') as td:
        work=Path(td); obj=work/'owner.o'
        subprocess.run([compiler(), *flags(work), '-c', str(ROOT/OWNER), '-o', str(obj)], check=True)
        nm=str(Path(compiler()).with_name('i386-elf-nm'))
        dump=str(Path(compiler()).with_name('i386-elf-objdump'))
        symbols=subprocess.check_output([nm, '-u', str(obj)], text=True)
        disasm=subprocess.check_output([dump, '-dr', str(obj)], text=True)
        assert not re.search(r'__(?:u?div|u?mod)di3', symbols + disasm)
        # Keep the real owner/prepare and stubs, discard only the independent
        # oracle vectors (which deliberately divide to compute expectations).
        link_source=(ROOT/'tools/tests/splash_v2_host.c').read_text()
        link_source=link_source.replace('void _start(void)', 'void unused_vectors(void)')
        link_source=re.sub(r'#include "../../([^"\n]+)"',
                           lambda m: '#include "'+str(ROOT/m[1])+'"', link_source)
        link_source += '\nvoid _start(void) { setup(1); u32 end=PLATFORM_SPLASH_IMAGE_TICKS; finish(run(&end)); }\n'
        source=work/'link.c'; source.write_text(link_source)
        exe=work/'owner-linked'
        subprocess.run([compiler(), *flags(work), '-ffunction-sections', '-fdata-sections',
                        '-nostdlib', '-Wl,--gc-sections', str(source), '-o', str(exe)], check=True)
        assert not subprocess.check_output([nm, '-u', str(exe)], text=True).strip()
        assert host32.run([str(exe)], runner='qemu', capture_output=True).returncode == 0
    assert not any('platform_splash_try_image_until' in (ROOT/p).read_text() for p in
                   ('include/platform_splash.h', OWNER, 'init/boot_splash.c'))
    print('GREEN owner object/disassembly/link without libgcc: no 64-bit division helpers; obsolete entry absent', flush=True)
def main():
    p=argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true'); args=p.parse_args()
    assert execute() == 0
    print('GREEN A4/A5 ownership, 31/32 OUT, fixed, preview once, both modes, wrap/stalled ticks, DROP/NO_SYNC, residual and cleanup', flush=True)
    no_division()
    if args.mutate:
        for mutation in MUTATIONS:
            assert execute(mutation) != 0, 'survived: '+mutation[0]
            print('RED '+mutation[0]+': compiled, runtime failure', flush=True)
        assert execute() == 0
        print('GREEN restored owner', flush=True)
def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_splash_v2.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
if __name__ == '__main__': main()
