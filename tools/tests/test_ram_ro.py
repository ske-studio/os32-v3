"""Exercise the RAM root device with the production device registry."""
CHECK = dict(name='ram-ro-host', tier=1,
    covers=['build/core.yaml', 'userland/deploy.yaml', 'tools/deploy_manifests.py', 'tools/mkpkg.py', 'tools/mkfat12.py', 'build/packages.yaml', 'drivers/block/ram_ro.c', 'drivers/block/ram_ro.h',
            'tools/tests/ram_ro_host.c','drivers/base/dev.c', 'drivers/base/dev.h', 'include/boot_root.h',
            'tools/tests/ram_iso_host.c', 'tools/mkroot_image.py', 'fs/isofs/**', 'fs/vfs.c', 'fs/vfs.h', 'fs/vfs_device.h', 'lib/kstring_c.c', 'include/types.h', 'lib/kstring.h', 'sdk/include/os32/os32_kapi_shared.h'],
    needs=['make:all'], budget=5)

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


def iso_test(directory, mutate):
    d = pathlib.Path(directory)
    (d / 'motd').write_bytes(b'RAM root!\n!')
    subprocess.run([sys.executable, '-B', 'tools/mkroot_image.py', '--motd', str(d / 'motd'),
                    '--out', str(d / 'root.iso')], cwd=ROOT, check=True)
    original = (ROOT / 'fs/isofs/iso9660.c').read_text()
    anchor = "devname[0] = type == VFS_DEV_RAM ? 'r' : 'c';"
    for variant in range(1 + int(mutate)):
        if original.count(anchor) != 1:
            raise SystemExit('ISO mutation anchor is not unique')
        source = d / 'iso9660.c'
        source.write_text(original.replace(anchor, "devname[0] = 'c';") if variant else original)
        cmd = ['gcc-13', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffunction-sections',
               '-fdata-sections', '-Wl,--gc-sections', '-D__cdecl=', '-I.', '-Iinclude',
               '-Isdk/include/os32', '-Ifs', '-Ifs/isofs', '-Idrivers/base', '-Idrivers/ata', '-Imm', '-Ilib',
               str(source), 'fs/isofs/iso9660_media.c', 'fs/vfs.c', 'drivers/base/dev.c',
               'drivers/block/ram_ro.c', 'lib/kstring_c.c', 'tools/tests/ram_iso_host.c', '-o', str(d / 'iso-test')]
        subprocess.run(cmd, cwd=ROOT, check=True)
        rc = subprocess.run([str(d / 'iso-test'), str(d / 'root.iso')], cwd=ROOT).returncode
        if rc != (3 if variant else 0):
            raise SystemExit(f'ISO unexpected exit: {rc}')
        print('RED ISO rd name (exit 3)' if variant else 'RAM ISO: OK')


def main():
    with tempfile.TemporaryDirectory() as tmp:
        iso_test(tmp, '--mutate' in sys.argv)
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
