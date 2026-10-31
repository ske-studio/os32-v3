"""T5c-2b real adapter/runtime rollback and PCM playback (ILP32 runtime, LP64 device fixture).
Port I/O and relocated init are explicit host seams; no hardware claim.
"""
CHECK = dict(name='pcm-module-host', tier=1,
    covers=['kernel/irq/tick.c', 'kernel/irq/tick.h', 'platform/pc98/tick_order.h', 'include/time_units.h', 'kernel/module/module_runtime.c', 'kernel/module/module_runtime.h', 'kernel/module/module_image.c', 'kernel/module/module_image.h', 'kernel/module/module_schema.h', 'mm/pgalloc.h', 'include/types.h', 'include/memmap.h', 'fs/vfs.c', 'fs/vfs.h', 'drivers/base/dev.c', 'drivers/base/dev.h', 'drivers/block/disk.h', 'drivers/ata/atapi.h', 'drivers/block/fdc.h', 'include/console.h', 'include/kbd.h', 'drivers/input/kbd.h', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'lib/kstring.h', 'tools/tests/module_runtime_host.c', 'tools/tests/test_module_runtime.py', 'tools/tests/module_runtime_tdd.md', 'tools/tests/test_module_image.py', 'tools/tests/module_image_host.c', 'tools/tests/host32.py', 'drivers/sound/cs4231/pcm_module.c', 'drivers/sound/cs4231/pcm_module.h', 'drivers/sound/cs4231/pcm_body.c', 'include/config.h', 'sdk/include/os32/os32_generations.h', 'include/bootinfo.h', 'tools/prototypes/pcm_imports.def', 'tools/gen_sqlite_imports.py', 'tools/tests/pcm_module_host.c', 'build/pcm-module.ld', 'drivers/sound/cs4231/pcm_cs4231.c', 'drivers/sound/cs4231/pcm_cs4231_math.c', 'drivers/sound/cs4231/pcm_cs4231.h', 'kernel/irq/irq.h', 'kernel/irq/irq_math.h', 'mm/dma_pool.h', 'drivers/dma/dma8237.h', 'mm/kmalloc.h', 'include/sys.h', 'include/kprintf.h', 'tools/tests/pcm_module_playback_host.c', 'tools/tests/pcm_cs4231_host.c', 'tools/tests/test_pcm_cs4231.py', 'tools/tests/pcm_hostshim/io.h', 'tools/tests/mutpar.py', 'kernel/module/kdebug_imports.def', 'tools/mkmod.py', 'tools/gen_kdebug_imports.py', 'sdk/kapi.json', 'build/out/pcm/*', 'build/out/pcm.mod', 'build/out/vmkernel.lz4', 'build/out/kernel.elf', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'include/pcm_contract.h', 'include/dma_contract.h', 'tools/tests/kernel_includes.py', 'lib/sqlite3/os32_sqlite_vfs.c', 'tools/prototypes/sqlite_imports.def'],
    needs=['make:all'], budget=4.72497)

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
SOURCE = ROOT / 'drivers/sound/cs4231/pcm_module.c'
CASES = ['normal-detach-reload', 'failure-after-irq', 'dma-stop-failure-isolates',
         'prefix-failure', 'irq-line-failures-detach-reload', 'irq-detach-failure-retains']

def product_image(folder):
    """Real mkmod product: PAIR/phase and relocation, with write-free rejection."""
    facade = SOURCE.read_text()
    assert 'pcm_cs4231.h' not in facade and 'PCM_IRQ' not in facade
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    metadata = json.loads((ROOT / 'build/out/pcm/pcm_imports.json').read_text())
    data = (ROOT / 'build/out/pcm.mod').read_bytes()
    kernel = (ROOT / 'build/out/vmkernel.lz4').read_bytes()
    # Use the existing packer for the boot loader's paired image CRC.
    from sys import path
    path.insert(0, str(ROOT / 'tools'))
    import mkmod
    pair = mkmod.pair_crc(kernel)
    rebuilt, stats = mkmod.build((ROOT / 'build/out/pcm/pcm.elf').read_bytes(), metadata,
                                 kernel, spec['version'], spec['generations']['memory_layout'],
                                 module_id=7)
    assert rebuilt == data
    names = {entry['name'] for entry in metadata['imports']}
    assert {'pcm_module_irq_register', 'pcm_module_irq_unregister',
            'pcm_module_dma_alloc', 'pcm_module_dma_free', 'dma_mark_leaked',
            'ktime_ticks'} <= names
    assert not {'irq_register', 'irq_unregister', 'dma_alloc', 'dma_free', 'tick_count'} & names
    core = mkmod.Elf((ROOT / 'build/out/kernel.elf').read_bytes())
    body = mkmod.Elf((ROOT / 'build/out/pcm/pcm.elf').read_bytes())
    for name in ('pcm_init', 'pcm_module_quiet', 'g_pcm', 'pcm_module_init'):
        assert name not in core.symbols and name in body.symbols, name
    for name in ('pcm_open', 'pcm_state', 'pcm_tick', 'pcm_reclaim',
                 'dma_alloc', 'dma_free', 'irq_register', 'vfs_read'):
        assert name in core.symbols, name
    # h4's core VFS/DMA imports and S4's PCM imports retain their addresses.
    imports = (ROOT / 'kernel/module/kdebug_imports.def').read_text()
    for name in ('pcm_open', 'pcm_state', 'dma_alloc', 'dma_free', 'vfs_read'):
        assert '"symbol": "' + name + '"' in imports
    exe = build_validator(folder)
    contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                    abi=metadata['module_abi_gen'], pair=pair, ops=1, img_valid=1, module_id=7,
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
    print('PASS product PCM: paired POST_ROOT, relocate, truncated/corrupt rejection, core dependencies', flush=True)


def driver_checks(folder, omit_evidence):
    """Real CS4231 shutdown with the existing port/clock/pool fixture.
    Model I/O is not a claim of PCM transfer on NP21/W or real hardware.
    """
    from test_pcm_cs4231 import FLAGS as PCM_FLAGS, INCLUDES as PCM_INCLUDES
    driver = (ROOT / 'drivers/sound/cs4231/pcm_cs4231.c').read_text()
    if omit_evidence:
        old = "evid = ((inp(PCM_PORT_R0) & PCM_R0_INIT) == 0 &&\n                (cs_read(PCM_I_IFACE) & PCM_IFACE_PEN) == 0);"
        assert driver.count(old) == 1
        driver = driver.replace(old, 'evid = 1; /* omit DMA stop evidence */')
    source = folder / 'pcm-driver.c'
    source.write_text(driver)
    harness = (ROOT / 'tools/tests/pcm_module_playback_host.c').read_text()
    harness = harness.replace('"pcm_cs4231_host.c"', '"' + str(ROOT / 'tools/tests/pcm_cs4231_host.c') + '"')
    # Include the fixture with its actual driver replaced, without a second body.
    fixture = (ROOT / 'tools/tests/pcm_cs4231_host.c').read_text()
    fixture = fixture.replace('"../../drivers/sound/cs4231/pcm_cs4231.c"', '"' + str(source) + '"')
    fixture = fixture.replace('"../../drivers/sound/cs4231/pcm_cs4231_math.c"', '"' + str(ROOT / 'drivers/sound/cs4231/pcm_cs4231_math.c') + '"')
    local = folder / 'pcm-fixture.c'
    local.write_text(fixture)
    harness = harness.replace('"' + str(ROOT / 'tools/tests/pcm_cs4231_host.c') + '"', '"' + str(local) + '"')
    probe = folder / 'pcm-playback.c'
    probe.write_text(harness)
    exe = folder / 'pcm-playback'
    subprocess.run(['gcc', *PCM_FLAGS, '-no-pie',
                    *PCM_INCLUDES,
                    str(probe), '-o', str(exe)], check=True)
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        result = subprocess.run((['qemu-x86_64'] if runner == 'qemu' else []) + [str(exe)],
                                capture_output=True, timeout=60)
        assert result.returncode == int(omit_evidence), result
        if omit_evidence:
            assert b'FAIL ' in result.stderr, result
    print(('MUTATE RED DMA evidence omitted (compiled)' if omit_evidence else
           'PASS real PCM: play/close, IRQ rollback, quiet/reclaim, retained ring, 64KiB'), flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='pcm-module-') as td:
        folder = Path(td)
        product_image(folder)
        exe = folder / 'pcm-module'
        def build(source):
            subprocess.run(['gcc', *FLAGS, '-Wno-unused-parameter', *INCLUDES,
                            '-I' + str(ROOT / 'build/out/pcm'),
                            '-DPCM_ADAPTER="' + str(source) + '"',
                            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                            ROOT / 'tools/tests/pcm_module_host.c',
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
        driver_checks(folder, False)
        if '--mutate' in sys.argv or '--prepare' in sys.argv:
            source = SOURCE.read_text()
            for label, old, new, modes in (
                ('tick-detach', 'tick_target = 0;', '/* omitted tick detach */', [1]),
                ('irq-detach', 'irq_target = 0;', '/* omitted IRQ detach */', [1]),
                ('irq-line', 'irq_line = irq;', 'irq_line = irq ^ 1U;', [4]),
            ):
                assert source.count(old) == 1, label
                mutant = folder / (label + '.c')
                mutant.write_text(source.replace(old, new))
                build(mutant)
                print('PASS prepare ' + label + ': unique anchor, compiled -Werror', flush=True)
                if '--mutate' in sys.argv:
                    run(modes, expected=1)
                    print('MUTATE ' + label + ' RED (runtime assertions; compiled)', flush=True)
            if '--mutate' in sys.argv:
                driver_checks(folder, True)
    return 0

if __name__ == '__main__':
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_pcm_module.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
