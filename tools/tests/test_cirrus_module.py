"""T5c-5b real body registration/runtime rollback; hardware callbacks are seams.
The existing Cirrus windows and three-backend suites cover drawing/lease paths.
"""
import os
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import host32
from test_module_runtime import FLAGS, INCLUDES, image
from test_module_image import build_validator, validate, fnv, crc, word
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'kernel/cirrus_body.c'
CASES = ['register-detach', 'failure-after-registration', 'selected-isolation', 'prefix-failure']

def product_image(folder):
    """Real mkmod product: PAIR/phase and relocation, with write-free rejection."""
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    metadata = json.loads((ROOT / 'build/out/cirrus/cirrus_imports.json').read_text())
    data = (ROOT / 'build/out/cirrus.mod').read_bytes()
    kernel = (ROOT / 'build/out/vmkernel.lz4').read_bytes()
    # Use the existing packer for the boot loader's paired image CRC.
    from sys import path
    path.insert(0, str(ROOT / 'tools'))
    import mkmod
    pair = mkmod.pair_crc(kernel)
    rebuilt, stats = mkmod.build((ROOT / 'build/out/cirrus/cirrus.elf').read_bytes(), metadata,
                                 kernel, spec['version'], spec['generations']['memory_layout'],
                                 module_id=10)
    assert rebuilt == data
    core = mkmod.Elf((ROOT / 'build/out/kernel.elf').read_bytes())
    body = mkmod.Elf((ROOT / 'build/out/cirrus/cirrus.elf').read_bytes())
    for name in ('wab_cirrus_probe', 'wab_cirrus_setup_8bpp', 'wab_glue_xe10', 'cirrus_module_init'):
        assert name not in core.symbols and name in body.symbols, name
    for name in ('cirrus_identify', 'gfx_boot_reserve', 'paging_init', 'gfx_backend_cirrus'):
        assert name in core.symbols, name
    assert 'cirrus_identify' in body.symbols  # generated function import
    exe = build_validator(folder)
    contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                    abi=metadata['module_abi_gen'], pair=pair, ops=1, img_valid=1, module_id=10,
                    imports=[(fnv(e['name']), e['type_ver'], e['kind']) for e in metadata['imports']],
                    exports=[(1, fnv('initialize'), 1)])
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        def check(payload, rc):
            result = validate(exe, payload, relocate_base=0x1200000, runner=runner, **contract)
            assert result.returncode == bool(rc), result
            assert result.stdout == (b'GREEN\n' if not rc else f'RED {rc:02}\n'.encode()), result
        check(data, 0)
        for field, value, rc in ((9, 0, 3), (31, pair ^ 1, 4), (8, 1, 3)):
            bad = bytearray(data)
            word(bad, field, value)
            check(crc(bad), rc)
        check(data[:-1], 3)
        bad = bytearray(data)
        bad[-1] ^= 1
        check(bad, 9)
    print('PASS product Cirrus: paired POST_ROOT, relocate, truncated/corrupt rejection, core identification / module drivers', flush=True)


def hal_context(unguarded=False, cui_first=False):
    """Real module context and HAL body with the existing three-backend fixture.
    Hardware probe/register I/O are the same seams as test_gfx_kernel_fb.
    """
    from test_gfx_kernel_fb import run_case
    fixture = (ROOT / 'tools/tests/gfx_kernel_fb_host.c').read_text()
    glue = '#include "' + str(ROOT / 'kernel/cirrus_body.c') + '"\n' + """
GfxBackend gfx_backend_cirrus;
static void (*boundary_real_init)(void);
static int boundary_init_calls;
static void boundary_count_init(void)
{
    boundary_init_calls++;
    boundary_real_init();
}
static const CirrusModuleContext fixture_context = {
    &gfx_backend_cirrus, &gfx_counters,
    &gfx_current_height, &gfx_flip_enabled, &gfx_display_page
};
"""
    fixture = fixture.replace('static void run(void)\n{', glue +
                              'static void run(void)\n{\n    cirrus_module_init(&gfx_backend_cirrus, &fixture_context);')
    # Compile the actual kernel GUI boundary statement into the real HAL fixture.
    boot = (ROOT / 'kernel/kernel.c').read_text()
    start = boot.index('            if (is_gui) {\n                /* Graphics boundary')
    boundary = boot[start:boot.index('gfx_client_to_gshell();', start)]
    boundary = next(line.strip() for line in boundary.splitlines()
                    if 'gfx_prepare_backend();' in line)
    if unguarded:
        assert boundary == 'if (!gfx_cirrus_probed()) gfx_prepare_backend();'
        boundary = 'gfx_prepare_backend();'
    marker = '    /* Run the real selection/lifecycle for every preference, plus AUTO. */'
    assert fixture.count(marker) == 1
    first_init = ('gfx_init(); diag_previous_gen = host_serial_value("[gfx] init gen="); '
                  'gfx_shutdown(); host_serial_reset();') if cui_first else ''
    fixture = fixture.replace(marker, """
    /* GUI=0 boot preparation has no Cirrus body yet; no enable or real probe. */
    GfxBackend registered = gfx_backend_cirrus;
    gfx_backend_cirrus = (GfxBackend){0};
    gfx_prepare_backend();
    CHECK(!gfx_cirrus_probed());
    gfx_backend_cirrus = registered;
    boundary_real_init = gfx_backend_cirrus.init;
    gfx_backend_cirrus.init = boundary_count_init;
    CHECK(boundary_init_calls == 0);
    """ + first_init + """
    /* Two GUI entries, without the gshell init: only the boundary's work. */
    """ + boundary + "\n    " + boundary + """
    CHECK(gfx_cirrus_probed());
    CHECK(boundary_init_calls == 1);
    gfx_backend_cirrus.init = boundary_real_init;
""" + marker)
    change = ('gfx/backend_cirrus.c', '#include "cirrus_module.h"',
              '#define OS32_CIRRUS_MODULE 1\n#include "cirrus_module.h"')
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        result = run_case(None, runner, extra_changes=[change],
                          fixture_body='unsigned int host_arch_if = 0x202U;\n' + fixture)
        if unguarded:
            assert result.returncode == 1 and 'FAIL boundary_init_calls == 1' in result.stdout, result
            print('MUTATE RED repeated GUI boundary init (compiled)', flush=True)
        else:
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
            print('PASS module context / two GUI boundaries, one Cirrus init: ' + result.stdout.strip(), flush=True)


def identify_enable_mutant(folder):
    from test_cirrus_win import host_cmd
    from mutpar import build_in_tree
    source = (ROOT / 'gfx/cirrus_identify.c').read_text()
    old = 'int cirrus_identify(void)\n{'
    assert source.count(old) == 1
    source = source.replace(old, old + '\n    gfx_backend_cirrus.init();')
    exe = folder / 'identify-enable'
    tree = build_in_tree(ROOT, folder / 'identify-tree',
                         {'gfx/cirrus_identify.c': source}, [host_cmd(exe)],
                         capture_output=True)  # A compile failure is a failure, never RED.
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        result = host32.run([str(exe)], cwd=str(tree), runner=runner, capture_output=True)
        assert result.returncode == 1 and b'enable_calls == 0' in result.stdout, result
    print('MUTATE RED core identify mixed with enable (compiled)', flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='cirrus-module-') as td:
        folder = Path(td)
        product_image(folder)
        hal_context()
        hal_context(cui_first=True)
        # Real boot call order: reserve/mapping before AS; no body at boot probe.
        boot = (ROOT / 'kernel/kernel.c').read_text()
        assert boot.index('    gfx_boot_reserve();') < boot.index('    exec_init();')
        assert boot.index('    gfx_prepare_backend();') < boot.index('    cirrus_module_load();')
        boundary = boot.index('            if (is_gui) {\n                /* Graphics boundary')
        assert boot.index('gfx_prepare_backend();', boundary) < boot.index('gfx_client_to_gshell();', boundary)
        exe = folder / 'cirrus-module'
        def build(source):
            subprocess.run(['gcc', *FLAGS, '-Wno-unused-parameter', *INCLUDES,
                            '-I' + str(ROOT / 'gfx'), '-I' + str(ROOT / 'build/out/cirrus'),
                            '-DCIRRUS_BODY="' + str(source) + '"',
                            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                            ROOT / 'tools/tests/cirrus_module_host.c',
                            ROOT / 'kernel/module_runtime.c', ROOT / 'kernel/module_image.c',
                            ROOT / 'fs/vfs.c', ROOT / 'drivers/dev.c', '-o', exe], check=True)
        def run(modes, expected=0):
            for mode in modes:
                payload = struct.pack('<2I', 10 if mode == 3 else 0, mode) + image()
                for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                    result = host32.run([str(exe)], input=payload, runner=runner, capture_output=True)
                    assert result.returncode == expected, (CASES[mode], runner, result)
                    if expected:
                        assert b'FAIL !probes && !enables' in result.stdout, result
                print(('MUTATE RED ' if expected else 'PASS ') + CASES[mode], flush=True)
        build(SOURCE)
        run(range(len(CASES)))
        if '--mutate' in sys.argv:
            hal_context(unguarded=True)
            identify_enable_mutant(folder)
            source = SOURCE.read_text()
            old = '    *out = cirrus_backend_ops;'
            assert source.count(old) == 1
            mutant = folder / 'early-probe.c'
            mutant.write_text(source.replace(old, old + '\n    out->probe(); out->init();'))
            build(mutant)
            run([0], expected=1)
            print('MUTATE RED registration mixed with probe/enable (compiled)', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())
