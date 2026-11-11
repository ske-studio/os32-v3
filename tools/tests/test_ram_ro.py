"""Exercise the RAM root device with the production device registry."""
CHECK = dict(name='ram-ro-host', tier=1,
    covers=['drivers/block/ram_ro.c', 'drivers/block/ram_ro.h',
            'tools/tests/ram_ro_host.c','drivers/base/dev.c', 'drivers/base/dev.h', 'include/boot_root.h',
            'include/types.h', 'lib/kstring.h', 'sdk/include/os32/os32_kapi_shared.h'],
    needs=[], budget=5)

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'drivers/block/ram_ro.c'
ANCHOR = 'ram->base + lba * RAM_RO_SECTOR_SIZE'


def compile_and_test(source, directory):
    exe = pathlib.Path(directory) / 'test'
    cmd = ['gcc-13', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
           '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
           '-D__cdecl=', '-I.', '-Iinclude', '-Isdk/include/os32',
           str(source), 'drivers/base/dev.c', 'tools/tests/ram_ro_host.c', '-o', str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    return subprocess.run([str(exe)], cwd=ROOT, check=False).returncode


def main():
    with tempfile.TemporaryDirectory() as tmp:
        if compile_and_test(SOURCE, tmp) != 0:
            raise SystemExit('baseline failed')
        if '--mutate' in sys.argv:
            code = SOURCE.read_text()
            if code.count(ANCHOR) != 1:
                raise SystemExit('mutation anchor is not unique')
            mutant = pathlib.Path(tmp) / 'ram_ro.c'
            mutant.write_text(code.replace(ANCHOR, 'ram->base + (lba + 1) * RAM_RO_SECTOR_SIZE'))
            # The product header stays beside the original source.
            mutant.write_text(mutant.read_text().replace('#include "ram_ro.h"', '#include "drivers/block/ram_ro.h"'))
            rc = compile_and_test(mutant, tmp)
            if rc != 3:
                raise SystemExit(f'mutation failed to produce expected RED: {rc}')
            print('RED 1/1')
    print('ram-root-host OK')


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    args = ['python3', '-B', 'tools/tests/test_ram_ro.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root)
