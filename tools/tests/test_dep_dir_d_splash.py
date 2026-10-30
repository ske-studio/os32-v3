"""L20: pre-move planar bytes, boot lifecycle and isolated mutation preparation."""
from kernel_includes import include_flags
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tools/tests/fixtures/dep_dir_d'
BACKEND = 'platform/pc98/splash.c'
HEADER = 'include/platform_splash.h'
HARNESS = 'tools/tests/dep_dir_d_splash_host.c'
FILES = ('arch/x86/arch_io.h', 'arch/x86/x86_flags.h', 'exec/surface_limits.h', 'gfx/gfx.h', 'gfx/gfx_internal.h', 'gfx/palette.h', 'include/gfx_format.h', 'include/gfx_hal.h', 'include/gfx_mode.h', 'include/io.h', 'include/memmap.h', 'include/pc98.h', 'include/pegc.h', 'include/tvram.h', 'include/types.h', 'init/boot_splash.c', 'include/platform_splash.h', 'lib/kstring.h', 'platform/pc98/platform_io.h', 'platform/pc98/splash.c', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h', 'tools/tests/dep_dir_d_splash_host.c')
MUTATIONS = (('mask_first = 0xFF >> (x1 % BITS_PER_BYTE);',
              'mask_first = 0xFF << (x1 % BITS_PER_BYTE);'),)


def once(text, old, new):
    assert text.count(old) == 1, old
    changed = text.replace(old, new, 1)
    assert changed.count(old) == 0 and changed.count(new) == 1
    return changed


def command(work):
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O0', '-Wall', '-Wextra',
            '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
            '-nostdlib', '-static', '-no-pie', '-D__KERNEL_BUILD__',
            *include_flags(work),
            str(work/HARNESS), '-o', str(work/'splash')]


def build(work, source, omit_header=False):
    for rel in FILES:
        dest = work/rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT/rel, dest)
    (work/'splash_source.inc').write_text(
        source + '\n' + (work/'init/boot_splash.c').read_text())
    if omit_header:
        (work/HEADER).unlink()
    subprocess.run(command(work), check=True)
    return work/'splash'


def trace(exe):
    result = host32.run([str(exe)], runner='qemu', capture_output=True)
    assert result.returncode == 0, result
    return result.stdout


def resident(text):
    block = re.search(r'^C_KERNEL = (.*)$', text, re.M).group(1).split()
    assert block.count(BACKEND) == 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--compile-mutations', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((FIXTURE/'splash.json').read_text())
    expected = gzip.decompress((FIXTURE/'splash.trace.gz').read_bytes())
    assert manifest['baseline'] == 'b400acb6'
    assert hashlib.sha256(expected).hexdigest() == manifest['trace_sha256']
    assert hashlib.sha256((ROOT/HARNESS).read_bytes()).hexdigest() == manifest['harness_sha256']
    source = (ROOT/BACKEND).read_text()
    old, new = MUTATIONS[0]
    try:
        once(source+'\n'+old, old, new)
    except AssertionError:
        pass
    else:
        raise AssertionError('ambiguous anchor accepted')
    for old, new in MUTATIONS:
        once(source, old, new)
    mapping = (ROOT/'tools/check_map.d/check-dep-dir-d-splash-host.yaml').read_text()
    for rel in FILES:
        assert '\n- '+rel+'\n' in mapping, rel
    make = subprocess.run(['make', '-qp'], cwd=ROOT, capture_output=True, text=True)
    assert make.returncode in (0, 1), make.stderr
    resident(make.stdout)
    generic = (ROOT/'init/boot_splash.c').read_text()
    for token in ('splash_fb', 'GFX_BB_PLANAR4', 'GFX_PREF_PC98', 'GFX_PLANE_SZ',
                  'GFX_BPL', 'GVRAM_PLANES', 'bb_fill_rect', 'splash_gfx_init'):
        assert token not in generic, token
    with tempfile.TemporaryDirectory(prefix='dep-dir-d-splash-') as td:
        assert trace(build(Path(td), source)) == expected
    print('PASS L20 fixed planar bytes, clipping, all colors/planes, preference, invalid states, retry, palette/raster/cleanup order')
    if args.compile_mutations or args.mutate:
        for old, new in MUTATIONS:
            with tempfile.TemporaryDirectory(prefix='dep-dir-d-splash-prep-') as td:
                exe = build(Path(td), once(source, old, new))
                if args.mutate:
                    assert trace(exe) != expected
        print('PASS unique mutation anchors and isolated -Werror mutation builds')
    if args.mutate:
        try:
            resident(make.stdout.replace(BACKEND+' ', ''))
        except AssertionError:
            pass
        else:
            raise AssertionError('resident omission escaped')
    if args.compile_mutations or args.mutate:
        with tempfile.TemporaryDirectory(prefix='dep-dir-d-splash-header-') as td:
            try:
                build(Path(td), source, omit_header=True)
            except subprocess.CalledProcessError:
                print('PASS header omission rejected (not a runtime kill)')
            else:
                raise AssertionError('header leaked from original tree')


if __name__ == '__main__':
    main()
