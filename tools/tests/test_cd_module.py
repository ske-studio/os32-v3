"""T5b-5 CD registration lifetime: real adapter/runtime/registries, ILP32.

The injected init writes descriptors into actual allocated body pages. This
checks rollback, retained mount callbacks and prefix failure, not hardware I/O.
PIO, body init, corrupt ISO and package reads are covered by check-cd-read-host.
"""
CHECK = dict(name='cd-module-host', tier=1,
    covers=['sys/platform/pc98/bootinfo_wire.h', 'sys/platform/pc98/bootinfo_native.h', 'sys/kern/module/module_runtime.c', 'sys/kern/module/module_runtime.h', 'sys/kern/module/module_image.c', 'sys/kern/module/module_image.h', 'sys/kern/module/module_schema.h', 'sys/kern/mm/pgalloc.h', 'sys/include/types.h', 'sys/include/memmap.h', 'sys/fs/vfs.c', 'sys/fs/vfs.h', 'sys/dev/base/dev.c', 'sys/hal/block/dev.h', 'sys/dev/block/disk.h', 'sys/dev/ata/atapi.h', 'sys/dev/block/fdc.h', 'sys/include/console.h', 'sys/include/kbd.h', 'sys/platform/pc98/input/kbd.h', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'sys/kern/kstring.h', 'tools/tests/module_runtime_host.c', 'tools/tests/test_module_runtime.py', 'tools/tests/module_runtime_tdd.md', 'tools/tests/test_module_image.py', 'tools/tests/module_image_host.c', 'tools/tests/host32.py', 'sys/dev/ata/cd_module.c', 'sys/dev/ata/cd_module.h', 'sys/dev/ata/cd_body.c', 'sys/include/config.h', 'sdk/include/os32/os32_generations.h', 'sys/include/bootinfo.h', 'sys/hal/time/cpu_calibrate.h', 'tools/prototypes/cd_imports.def', 'tools/gen_sqlite_imports.py', 'tools/tests/cd_module_host.c', 'build/cd-module.ld', 'tools/mkmod.py', 'tools/gen_kdebug_imports.py', 'sdk/kapi.json', 'build/out/cd/*', 'build/out/cd.mod', 'build/out/vmkernel.lz4', 'build/out/kernel.elf', 'sys/fs/vfs_device.h', 'sys/platform/pc98/boot_names.h', 'sys/platform/pc98/boot_names.c', 'sys/dev/ata/ide.h', 'tools/tests/kernel_includes.py', 'sys/kern/module/kdebug_imports.def', 'lib/sqlite3/os32_sqlite_vfs.c', 'tools/prototypes/sqlite_imports.def'],
    needs=['make:all'], budget=4.9,
    kind='characterization',
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
SOURCE = ROOT / 'sys/dev/ata/cd_module.c'
CASES = ['register-unmount-release', 'fs-full', 'device-full', 'init-failed',
         'no-drive-iso-available', 'prefix-return-failed', 'mounted-body-retained']


def product_image(folder):
    """Real mkmod product: PAIR/phase and relocation, with write-free rejection."""
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    metadata = json.loads((ROOT / 'build/out/cd/cd_imports.json').read_text())
    data = (ROOT / 'build/out/cd.mod').read_bytes()
    kernel = (ROOT / 'build/out/vmkernel.lz4').read_bytes()
    # Use the existing packer for the boot loader's paired image CRC.
    from sys import path
    path.insert(0, str(ROOT / 'tools'))
    import mkmod
    pair = mkmod.pair_crc(kernel)
    rebuilt, stats = mkmod.build((ROOT / 'build/out/cd/cd.elf').read_bytes(), metadata,
                                 kernel, spec['version'], spec['generations']['memory_layout'],
                                 module_id=4)
    assert rebuilt == data
    core = mkmod.Elf((ROOT / 'build/out/kernel.elf').read_bytes())
    body = mkmod.Elf((ROOT / 'build/out/cd/cd.elf').read_bytes())
    for name in ('atapi_init', 'atapi_read_sectors', 'iso9660_ops'):
        assert name not in core.symbols and name in body.symbols, name
    for name in ('ide_init', 'fdc_read_sector', 'disk_read', 'dev_blk_read_lba'):
        assert name in core.symbols, name
    exe = build_validator(folder)
    contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                    abi=metadata['module_abi_gen'], pair=pair, ops=1, img_valid=1, module_id=4,
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
    print('PASS product CD: paired POST_ROOT, relocate, truncated/corrupt rejection, core dependencies', flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='cd-module-') as td:
        folder = Path(td)
        product_image(folder)
        exe = folder / 'cd-module'

        def build(source):
            subprocess.run(['gcc', *FLAGS, '-Wno-unused-parameter', *INCLUDES,
                            '-I' + str(ROOT / 'build/out/cd'),
                            '-DCD_ADAPTER="' + str(source) + '"',
                            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                            ROOT / 'tools/tests/cd_module_host.c',
                            ROOT / 'sys/kern/module/module_runtime.c', ROOT / 'sys/kern/module/module_image.c',
                            ROOT / 'sys/fs/vfs.c', ROOT / 'sys/dev/base/dev.c', '-o', exe], check=True)

        def run(modes, expected=0):
            for mode in modes:
                payload = struct.pack('<2I', 10 if mode == 5 else 0, mode) + image()
                for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                    result = host32.run([str(exe)], input=payload, runner=runner,
                                        capture_output=True)
                    assert result.returncode == expected, (CASES[mode], runner, result)
                    if expected:
                        assert b'FAIL ' in result.stdout, result
                print(('MUTATE RED ' if expected else 'PASS ') + CASES[mode], flush=True)

        build(SOURCE)
        run(range(len(CASES)))
        if '--mutate' in sys.argv:
            source = SOURCE.read_text()
            for label, old, new, modes in (
                ('retain-mounted-body', 'if (vfs_unregister_fs(candidate.fs) != VFS_OK) return 0;',
                 '(void)vfs_unregister_fs(candidate.fs);', [6]),
                ('device-detach', 'if (dev_unregister(candidate.device)) return 0;',
                 'if (0) return 0;', [0]),
                ('registration-result', 'if (rc) {', 'if (0) {', [1, 2]),
            ):
                assert old in source, label
                mutant = folder / (label + '.c')
                mutant.write_text(source.replace(old, new))
                build(mutant)
                run(modes, expected=1)
                print('MUTATE ' + label + ' RED (runtime assertions)', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_cd_module.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
