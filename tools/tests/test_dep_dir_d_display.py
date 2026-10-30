"""L19: fixed pre-move restore/ready/cell traces; isolated resident closure."""
CHECK = dict(name='dep-dir-d-display-host', tier=1,
    covers=['arch/x86/arch_io.h', 'arch/x86/x86_flags.h', 'exec/surface_limits.h', 'gfx/gfx.h', 'gfx/gfx_internal.h', 'gfx/palette.h', 'include/con_sink.h', 'include/console.h', 'include/gfx_format.h', 'include/io.h', 'include/memmap.h', 'include/pc98.h', 'include/pegc.h', 'include/types.h', 'kernel/console/display_restore.h', 'include/platform_display.h', 'kernel/v86/v86_bios.h', 'platform/pc98/display.c', 'platform/pc98/display_private.h', 'platform/pc98/platform_io.h', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h', 'tools/tests/dep_dir_d_display_host.c', 'tools/tests/host32.py', 'tools/tests/fixtures/dep_dir_d/**', 'build/checks.d/check-dep-dir-d-display-host.mk', 'tools/check_map.d/check-dep-dir-d-display-host.yaml', 'tools/check_map.yaml', 'kernel/v86/v86.c', 'kernel/v86/v86_gp_emulate.inc', 'drivers/video/pc98/pc98_route.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=60.0)

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
BACKEND = 'platform/pc98/display.c'
HEADER = 'platform/pc98/display_private.h'
HARNESS = 'tools/tests/dep_dir_d_display_host.c'
# Filled from the compiler's include closure, then fixed in source and check_map.
FILES = ('drivers/video/pc98/pc98_route.h', 'arch/x86/arch_io.h', 'arch/x86/x86_flags.h', 'exec/surface_limits.h', 'gfx/gfx.h', 'gfx/gfx_internal.h', 'gfx/palette.h', 'include/con_sink.h', 'include/console.h', 'include/gfx_format.h', 'include/io.h', 'include/memmap.h', 'include/pc98.h', 'include/pegc.h', 'include/types.h', 'kernel/console/display_restore.h', 'include/platform_display.h', 'kernel/v86/v86_bios.h', 'platform/pc98/display.c', 'platform/pc98/display_private.h', 'platform/pc98/platform_io.h', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h', 'tools/tests/dep_dir_d_display_host.c')
MUTATIONS = (
    ('0x00, 0x00, 0x00, 0x19,', '0x00, 0x00, 0x00, 0x18,'),
    ('== PEGC_HSYNC_31KHZ', '!= PEGC_HSYNC_31KHZ'),
    ('outp(MODE_FF1_PORT, MFF1_KCG_CODE);\n    io_wait();',
     'outp(MODE_FF1_PORT, MFF1_KCG_CODE);\n    (void)0;'),
)


def once(text, old, new):
    assert text.count(old) == 1, old
    changed = text.replace(old, new, 1)
    assert changed.count(old) == 0 and changed.count(new) == 1
    return changed


def instrument(source):
    source = re.sub(r'\*\(volatile (u8|u16) \*\)P2V_IO\(([^\n;]+)\) = ([^;]+);',
                    lambda m: f'store({m[2]}, sizeof({m[1]}), {m[3]});', source)
    source = re.sub(r'\*\(volatile (u8|u16) \*\)P2V_IO\(([^\n;]+)\)',
                    lambda m: f'load({m[2]}, sizeof({m[1]}))', source)
    assert 'P2V_IO' not in source
    return source


def command(work):
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O0', '-Wall', '-Wextra',
            '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
            '-nostdlib', '-static', '-no-pie', '-D__KERNEL_BUILD__',
            *include_flags(work),
            str(work/HARNESS), '-o', str(work/'display')]


def build(work, source, omit_header=False):
    for rel in FILES:
        dest=work/rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT/rel, dest)
    (work/'display_source.inc').write_text(instrument(source))
    if omit_header:
        (work/HEADER).unlink()
    subprocess.run(command(work), check=True)
    return work/'display'


def trace(exe):
    result=host32.run([str(exe)], runner='qemu', capture_output=True)
    assert result.returncode == 0, result
    return result.stdout


def resident(text):
    block=re.search(r'^C_KERNEL = (.*)$', text, re.M).group(1).split()
    assert block.count(BACKEND) == 1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--compile-mutations', action='store_true')
    args=parser.parse_args()
    manifest=json.loads((FIXTURE/'display.json').read_text())
    expected=gzip.decompress((FIXTURE/'display.trace.gz').read_bytes())
    assert manifest['baseline'] == 'b400acb6'
    assert hashlib.sha256(expected).hexdigest() == manifest['trace_sha256']
    assert hashlib.sha256((ROOT/HARNESS).read_bytes()).hexdigest() == manifest['harness_sha256']
    source=(ROOT/BACKEND).read_text()
    # A duplicated anchor must be rejected before any compilation.
    old,new=MUTATIONS[0]
    try:
        once(source+'\n'+old,old,new)
    except AssertionError:
        pass
    else:
        raise AssertionError('ambiguous anchor accepted')
    for old,new in MUTATIONS:
        once(source,old,new)
    mapping=(ROOT/'tools/check_map.d/check-dep-dir-d-display-host.yaml').read_text()
    for rel in FILES:
        assert '\n- '+rel+'\n' in mapping, rel
    make=subprocess.run(['make','-qp'],cwd=ROOT,capture_output=True,text=True)
    assert make.returncode in (0,1), make.stderr
    resident(make.stdout)
    generic=(ROOT/'kernel/v86/v86.c').read_text()
    for name in ('display_out','display_text_start','display_restore_98','display_recover_ready',
                 'display_recover_cell_read','display_recover_cell_write'):
        assert not re.search(r'^(?:static )?(?:void|int) '+name+r'\(',generic,re.M),name
    with tempfile.TemporaryDirectory(prefix='dep-dir-d-display-') as td:
        assert trace(build(Path(td),source)) == expected
    print('PASS L19 fixed BDA 8 combinations, restore/wait/state order, all ready gates, cell widths; no v86.mod linked')
    if args.compile_mutations or args.mutate:
        for old,new in MUTATIONS:
            with tempfile.TemporaryDirectory(prefix='dep-dir-d-display-prep-') as td:
                exe=build(Path(td),once(source,old,new))
                if args.mutate:
                    assert trace(exe) != expected
        print('PASS unique mutation anchors and isolated -Werror mutation builds')
    if args.mutate:
        try:
            resident(make.stdout.replace(BACKEND+' ',''))
        except AssertionError:
            pass
        else:
            raise AssertionError('resident omission escaped')
    if args.compile_mutations or args.mutate:
        with tempfile.TemporaryDirectory(prefix='dep-dir-d-display-header-') as td:
            try:
                build(Path(td),source,omit_header=True)
            except subprocess.CalledProcessError:
                print('PASS header omission rejected (not a runtime kill)')
            else:
                raise AssertionError('header leaked from original tree')


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_d_display.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
