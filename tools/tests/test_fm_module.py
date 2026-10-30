"""T5c-2a real adapter/runtime rollback and FM/snd playback, ILP32.
Port I/O and relocated init are explicit host seams; no hardware claim.
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
SOURCE = ROOT / 'drivers/sound/fm/fm_module.c'
CASES = ['normal-detach', 'failure-after-tick', 'key-off-timeout-isolates', 'prefix-failure']

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
    assert [(e['name'], e['irq']) for e in metadata['imports']] == [
        ('ktime_ticks', True), ('res_owner_get', False),
        ('ring3_wait_pending', False), ('ring3_abort_check', False)]
    core = mkmod.Elf((ROOT / 'build/out/kernel.elf').read_bytes())
    body = mkmod.Elf((ROOT / 'build/out/fm/fm.elf').read_bytes())
    for name in ('snd_init', 'fm_quiet', 'fm_module_init'):
        assert name not in core.symbols and name in body.symbols, name
    for name in ('ide_init', 'fdc_read_sector', 'disk_read', 'dev_blk_read_lba', 'snd_tick', 'fm_play_mml_kapi'):
        assert name in core.symbols, name
    exe = build_validator(folder)
    contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                    abi=metadata['module_abi_gen'], pair=pair, ops=1, img_valid=1, module_id=6,
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
                            '-I' + str(ROOT / 'build/out/fm'),
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
        subprocess.run(['gcc', *FLAGS, '-Wno-unused-parameter', *INCLUDES,
                        '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                        ROOT / 'tools/tests/fm_playback_host.c', '-o', playback], check=True)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(playback)], runner=runner, capture_output=True)
            assert result.returncode == 0, result
        print('PASS real FM/snd: BGM, SE, owner exit, normal MML, STOP Key-OFF, silence BUSY timeout', flush=True)
        if '--mutate' in sys.argv:
            source = SOURCE.read_text()
            for label, old, new, modes in (
                ('tick-detach', 'tick_target = 0;', '/* omitted tick detach */', [1]),
                ('quiet-proof', 'return candidate.quiet();', 'return 1;', [2]),
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
