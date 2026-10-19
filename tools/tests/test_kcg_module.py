"""T5c-1: real KCG body, typed adapter/runtime, paired product and runtime RED."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import host32
from test_module_runtime import FLAGS, INCLUDES, image
from test_module_image import build_validator, validate, fnv, crc, word

ROOT = Path(__file__).resolve().parents[2]


def graphics_boundary(unguarded=False):
    from test_gfx_kernel_fb import run_case
    fixture = (ROOT / 'tools/tests/gfx_kernel_fb_host.c').read_text()
    fixture = fixture.replace('void shell_print(const char *s,u8 c) {(void)s;(void)c;}',
                              'static int messages;\nvoid shell_print(const char *s,u8 c) '
                              '{ (void)c; const char *want = "Error: KCG unavailable -> CUI\\n"; '
                              'while (*s && *s == *want) { s++; want++; } CHECK(*s == *want); messages++; }')
    fixture = fixture.replace('static void run(void)\n{', 'static void original_run(void)\n{')
    assert fixture.count('    die(0);') == 1
    fixture = fixture.replace('    die(0);', '    return;')
    fixture += '''
static void run(void)
{
    original_run();
    gfx_shutdown();
    u32 generation = gfx_init_gen;
    host_gui = 0;
    host_kcg_ready = 0;
    for (int mode = 0; mode < 2; mode++) {
        slot.abort_req = 0;
        if (mode) gfx_kapi_init_200(); else gfx_kapi_init();
        CHECK(slot.abort_req && gfx_init_gen == generation);
        CHECK(messages == mode + 1);
    }
    host_kcg_ready = 1;
    slot.abort_req = 0;
    gfx_kapi_init();
    CHECK(!slot.abort_req && gfx_init_gen == generation + 1);
    gfx_shutdown();
    gfx_kapi_init_200();
    CHECK(!slot.abort_req && gfx_init_gen == generation + 2);
    CHECK(messages == 2);
    die(0);
}
'''
    changes = [('gfx/gfx_core.c', 'if (!kcg_module_ready()) {', 'if (0) {')] if unguarded else []
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        result = run_case(None, runner, fixture_body='unsigned int host_arch_if = 0x202U;\n' + fixture,
                          extra_changes=changes)
        if unguarded:
            assert result.returncode == 1 and 'FAIL slot.abort_req && gfx_init_gen == generation' in result.stdout, result
            print('RED KCG graphics boundary removed (compiled, runtime assertion)', flush=True)
        else:
            assert result.returncode == 0, (result.stdout, result.stderr)
            print('PASS KCG graphics boundary: CUI init/init200 refuse before drawing, ready succeeds', flush=True)


def product(folder):
    sys.path.insert(0, str(ROOT / 'tools'))
    import mkmod
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    meta = json.loads((ROOT / 'build/out/kcg/kcg_imports.json').read_text())
    kernel = (ROOT / 'build/out/vmkernel.lz4').read_bytes()
    data = (ROOT / 'build/out/kcg.mod').read_bytes()
    rebuilt, _ = mkmod.build((ROOT / 'build/out/kcg/kcg.elf').read_bytes(), meta, kernel,
                            spec['version'], spec['generations']['memory_layout'], module_id=5)
    assert rebuilt == data
    core = mkmod.Elf((ROOT / 'build/out/kernel.elf').read_bytes())
    body = mkmod.Elf((ROOT / 'build/out/kcg/kcg.elf').read_bytes())
    assert not {'lz4_decode', 'boot_font_load', 'kanji_cache', 'ank_cache'} & core.symbols.keys()
    assert 'kcg_module_init' not in core.symbols and 'kcg_module_init' in body.symbols
    assert 'unicode_file_load' in core.symbols and 'unicode_file_load' not in body.symbols
    contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                    abi=meta['module_abi_gen'], pair=mkmod.pair_crc(kernel), ops=1,
                    img_valid=1, module_id=5,
                    imports=[(fnv(e['name']), e['type_ver'], e['kind']) for e in meta['imports']],
                    exports=[(1, fnv('initialize'), 1)])
    exe = build_validator(folder)
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        def check(payload, rc):
            result = validate(exe, payload, relocate_base=0x1200000, runner=runner, **contract)
            assert result.returncode == bool(rc), result
            assert result.stdout == (b'GREEN\n' if not rc else f'RED {rc:02}\n'.encode()), result
        check(data, 0)
        for field, value, rc in ((9, 0, 3), (31, mkmod.pair_crc(kernel)^1, 4)):
            bad = bytearray(data); word(bad, field, value); check(crc(bad), rc)
        check(data[:-1], 3)
    print('PASS KCG product: core Unicode, PAIR/phase/relocation/short read', flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='kcg-module-') as td:
        folder = Path(td)
        product(folder)
        adapter = (ROOT / 'kernel/kcg_module.c').read_text()
        body = (ROOT / 'kernel/kcg_body.c').read_text()

        def run(source, kind, modes, expected=0):
            path = folder / ('kcg_body_source.inc' if kind == 'body' else 'adapter.c')
            path.write_text(source)
            exe = folder / kind
            extra = ([f'-DKCG_ADAPTER="{path}"', ROOT / 'kernel/module_runtime.c',
                      ROOT / 'kernel/module_image.c', ROOT / 'fs/vfs.c', ROOT / 'drivers/dev.c']
                     if kind == 'module' else [])
            result = subprocess.run(['gcc', *FLAGS, '-Wno-unused-parameter', *INCLUDES,
                                     '-I'+str(ROOT/'build/out/kcg'), '-I'+str(folder),
                                     '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                                     ROOT / f'tools/tests/kcg_{kind}_host.c', *extra,
                                     '-o', exe], capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError('compile failure is not runtime RED\n'+result.stdout+result.stderr)
            for mode in modes:
                payload = struct.pack('<2I', 10 if mode == 3 else 0, mode) + image()
                for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                    result = host32.run([str(exe)], input=payload, runner=runner, capture_output=True)
                    assert result.returncode == expected, (kind, mode, result)
                    if expected: assert b'FAIL ' in result.stdout, result
                    else: print(result.stdout.decode().strip(), flush=True)

        run(adapter, 'module', range(6))
        print('PASS adapter: missing stub/diagnostic, allocation failure, failed init/export/prefix, abort, duplicate', flush=True)
        run(body, 'body', [0])
        graphics_boundary()
        if '--mutate' in sys.argv:
            graphics_boundary(unguarded=True)
            for label, source, old, new, kind, modes in (
                ('wrong-export', adapter, 'return MODULE_INIT_FAILED;\n    return 0;',
                 'return 0;\n    return 0;', 'module', [1]),
                ('adapter-double-init', adapter,
                 'if (kcg_handle.generation) return MODULE_BUSY;',
                 'if (0) return MODULE_BUSY;', 'module', [0]),
                ('cache-collision-stale', body, 'slot->tag == tag && tag',
                 'slot->tag != 0', 'body', [0]),
                ('allocation-failure-publishes', adapter,
                 'kmemset(buf, 0, KCG_KANJI_H * 2);',
                 'kmemset(buf, 1, KCG_KANJI_H * 2);', 'module', [4]),
                ('body-double-init', body, 'if (initialized) return MODULE_BUSY;',
                 'if (0) return MODULE_BUSY;', 'body', [0]),
            ):
                assert source.count(old) == 1, label
                run(source.replace(old, new), kind, modes, expected=1)
                print('RED '+label+' (compiled, runtime assertion)', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
