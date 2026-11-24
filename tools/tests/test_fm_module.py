"""T5c-2a real adapter/runtime rollback and FM/snd playback, ILP32.
Port I/O and relocated init are explicit host seams; no hardware claim.
"""
CHECK = dict(name='fm-module-host', tier=1,
    covers=['sound/hal/snd2_chip.c', 'drivers/sound/fm/opn_chip.h', 'drivers/sound/fm/opn_probe.h', 'drivers/sound/cs4231/pcm_cs4231.h', 'sdk/include/os32/*.h', 'sound/hal/snd2_int.h', 'include/av_contract.h', 'platform/pc98/module_pair.c', 'include/platform_module_pair.h', 'platform/pc98/bootinfo_wire.h', 'platform/pc98/bootinfo_native.h', 'kernel/irq/tick.c', 'kernel/irq/tick.h', 'platform/pc98/tick_order.h', 'kernel/module/module_runtime.c', 'kernel/module/module_runtime.h', 'kernel/module/module_image.c', 'kernel/module/module_image.h', 'kernel/module/module_schema.h', 'mm/pgalloc.h', 'include/types.h', 'include/memmap.h', 'fs/vfs.c', 'fs/vfs.h', 'drivers/base/dev.c', 'kernel/dev/dev.h', 'drivers/block/disk.h', 'drivers/ata/atapi.h', 'drivers/block/fdc.h', 'include/console.h', 'include/kbd.h', 'drivers/input/kbd.h', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'lib/kstring.h', 'tools/tests/module_runtime_host.c', 'tools/tests/test_module_runtime.py', 'tools/tests/module_runtime_tdd.md', 'tools/tests/test_module_image.py', 'tools/tests/module_image_host.c', 'tools/tests/host32.py', 'drivers/sound/fm/fm_module.c', 'drivers/sound/fm/fm_module.h', 'drivers/sound/fm/fm_body.c', 'include/config.h', 'sdk/include/os32/os32_generations.h', 'include/bootinfo.h', 'tools/prototypes/fm_imports.def', 'tools/gen_sqlite_imports.py', 'tools/tests/fm_module_host.c', 'build/fm-module.ld', 'drivers/sound/fm/fm.c', 'drivers/sound/fm/fm.h', 'tools/tests/fm_playback_host.c', 'tools/mkmod.py', 'tools/gen_kdebug_imports.py', 'sdk/kapi.json', 'build/out/fm/*', 'build/out/fm.mod', 'build/out/vmkernel.lz4', 'build/out/kernel.elf', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'include/fm_contract.h', 'tools/tests/kernel_includes.py', 'kernel/module/kdebug_imports.def', 'lib/sqlite3/os32_sqlite_vfs.c', 'tools/prototypes/sqlite_imports.def'],
    needs=['make:all'], budget=5.5,
    basis=['docs/decisions/sound/AV_HAL_DESIGN.md 追記2 §2・§5'],
    pins=['AV_HAL_DESIGN'],
)

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
SOURCE = ROOT / 'drivers/sound/fm/fm_module.c'
CASES = ['normal-detach', 'failure-after-tick', 'key-off-timeout-isolates', 'prefix-failure', 'unregister-failure']

def product_image(folder):
    """Real mkmod product: PAIR/phase and relocation, with write-free rejection."""
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    metadata = json.loads((ROOT / 'build/out/fm/fm_imports.json').read_text())
    data = (ROOT / 'build/out/fm.mod').read_bytes()
    kernel = (ROOT / 'build/out/vmkernel.lz4').read_bytes()
    # Use the existing packer for the boot loader's paired image CRC.
    from sys import path
    path.insert(0, str(ROOT / 'tools'))
    import mkmod
    pair = mkmod.pair_crc(kernel)
    rebuilt, stats = mkmod.build((ROOT / 'build/out/fm/fm.elf').read_bytes(), metadata,
                                 kernel, spec['version'], spec['generations']['memory_layout'],
                                 module_id=6)
    assert rebuilt == data
    assert [e['name'] for e in metadata['imports']] == [
        'snd2_chip_register', 'snd2_chip_unregister', 'snd2_chip_implicit']
    core = mkmod.Elf((ROOT / 'build/out/kernel.elf').read_bytes())
    body = mkmod.Elf((ROOT / 'build/out/fm/fm.elf').read_bytes())
    for name in ('opn_probe', 'opn_chip_backend', 'fm_module_init'):
        assert name not in core.symbols and name in body.symbols, name
    for name in ('ide_init', 'fdc_read_sector', 'disk_read', 'dev_blk_read_lba', 'fm_note_on', 'ssg_all_off'):
        assert name in core.symbols, name
    exe = build_validator(folder)
    contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                    abi=metadata['module_abi_gen'], pair=pair, ops=2, img_valid=1, module_id=6,
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
    print('PASS product FM: paired POST_ROOT, relocate, truncated/corrupt rejection, core dependencies', flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='fm-module-') as td:
        folder = Path(td)
        product_image(folder)
        exe = folder / 'fm-module'
        def build(source):
            subprocess.run(['gcc', *FLAGS, '-Wno-unused-parameter', *INCLUDES,
                            '-I' + str(ROOT), '-I' + str(ROOT / 'build/out/fm'),
                            '-DFM_ADAPTER="' + str(source) + '"',
                            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                            ROOT / 'tools/tests/fm_module_host.c',
                            ROOT / 'kernel/module/module_runtime.c', ROOT / 'kernel/module/module_image.c',
                            ROOT / 'fs/vfs.c', ROOT / 'drivers/base/dev.c', '-o', exe], check=True)
        def run(modes, expected=0):
            for mode in modes:
                payload = struct.pack('<2I', 10 if mode == 3 else 0, mode) + image()
                for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                    result = host32.run([str(exe)], input=payload, runner=runner, capture_output=True)
                    assert result.returncode == expected, (CASES[mode], runner, result)
                    if expected:
                        assert b'FAIL ' in result.stdout, result
                print(('MUTATE RED ' if expected else 'PASS ') + CASES[mode], flush=True)
        build(SOURCE)
        run(range(len(CASES)))
        playback = folder / 'playback'
        def run_playback(source, expected, body_source=ROOT / 'drivers/sound/fm/fm_body.c'):
            subprocess.run(['gcc', *FLAGS, '-Wno-unused-parameter', *INCLUDES,
                            '-I' + str(ROOT), '-DBODY_SOURCE="' + str(body_source) + '"',
                            '-DFM_SOURCE="' + str(source) + '"',
                            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                            ROOT / 'tools/tests/fm_playback_host.c', ROOT / 'sound/hal/snd2_chip.c', '-o', playback], check=True)
            for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                if not expected:
                    quiet = host32.run([str(playback)], runner=runner, input=b'q', capture_output=True)
                    assert quiet.returncode == 0, quiet
                result = host32.run([str(playback)], runner=runner, input=b'n', capture_output=True)
                assert result.returncode == expected, result
                if expected:
                    assert b'FAIL ' in result.stdout, result

        fm = ROOT / 'drivers/sound/fm/fm.c'
        run_playback(fm, 0)
        print('PASS real FM/SSG quiet, restart and BUSY timeout', flush=True)
        if '--mutate' in sys.argv:
            anchor = '(ssg_mixer_state & ~(1u << ch))'
            body = fm.read_text()
            assert body.count(anchor) == 1
            mutant = folder / 'ssg-restart.c'
            mutant.write_text(body.replace(anchor, 'ssg_mixer_state'))
            run_playback(mutant, 1)
            print('MUTATE SSG restart RED', flush=True)
            # Continue after the first failed pitch write.
            start = next(line for line in body.splitlines() if 'if (!legacy_write(OPN_REG_FNUM_HI' in line)
            assert body.count(start) == 1
            mutant.write_text(body.replace(start, start.replace('if (!legacy_write', '(void)legacy_write').replace(') return;', ';')))
            run_playback(mutant, 1)
            print('MUTATE first failed register write RED', flush=True)
            module_body = (ROOT / 'drivers/sound/fm/fm_body.c').read_text()
            for old, new in [('device = snd2_chip_register(&opn_chip_backend, &chip, &info);', '(void)info; device = 0;'),
                             ('device >= 0 && snd2_chip_unregister(device) < 0', '0'),
                             ('report_fault(device);', '(void)device;')]:
                assert module_body.count(old) == 1
                mutant.write_text(module_body.replace(old, new))
                run_playback(fm, 1, mutant)
                print('MUTATE RED ' + old, flush=True)
            source = SOURCE.read_text()
            for label, old, new, modes in (
                ('implicit', 'snd2_chip_implicit(res_owner_get()) == 0', '1', [0]),
                ('detach', '!candidate_valid || candidate.detach()', '1', [4]),
                ('tick-ref', '    candidate_valid = 1;',
                 '    candidate_valid = 1;\n    module_ref_acquire(fm_handle, r->owner, MODULE_REF_TICK, (u32)(uptr)candidate.fm_all_off);', [1]),
                ('note-forward', 'candidate.fm_note_on(ch, note);', '(void)note;', [0]),
                ('quiet-proof', 'return candidate_valid && candidate.quiet();', 'return 1;', [2]),
            ):
                assert source.count(old) == 1, label
                mutant = folder / (label + '.c')
                mutant.write_text(source.replace(old, new))
                build(mutant)
                run(modes, expected=1)
                print('MUTATE ' + label + ' RED (runtime assertions; compiled)', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_fm_module.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
