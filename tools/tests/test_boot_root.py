"""Exercise platform root boot with the production VFS and filesystems."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CHECK = dict(name='boot-root-host', tier=1, budget=30, needs=['make:all'],
             covers=['build/core.yaml', 'userland/deploy.yaml', 'tools/deploy_manifests.py', 'tools/mkpkg.py', 'tools/mkfat12.py', 'build/packages.yaml', 'init/boot_root.c', 'platform/powermac/root_boot.[ch]',
                     'tools/tests/boot_root_host.c', 'tools/tests/test_boot_root.py',
                     'include/boot_root.h', 'drivers/block/ram_ro.[ch]',
                     'drivers/base/dev.[ch]', 'fs/vfs*', 'fs/isofs/**',
                     'fs/ramfs/**', 'lib/kstring*', 'tools/mkroot_image.py'])


def main():
    source = (ROOT / 'init/boot_root.c').read_text()
    anchor = 'return vfs_mount("/tmp", "rd1", "ramfs");'
    if source.count(anchor) != 1:
        raise RuntimeError('mutation anchor is not unique')
    with tempfile.TemporaryDirectory() as tmp:
        d = pathlib.Path(tmp)
        (d / 'motd').write_bytes(b'RAM root!\n!')
        subprocess.run([sys.executable, '-B', 'tools/mkroot_image.py', '--motd', str(d / 'motd'),
                        '--out', str(d / 'root.iso')], cwd=ROOT, check=True)
        for mutant in range(1 + int('--mutate' in sys.argv)):
            modified = source.replace(anchor, 'return 0;') if mutant else source
            (d / 'boot_root.c').write_text(modified)
            cmd = ['gcc-13', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                   '-D__cdecl=', '-I.', '-Iinclude', '-Isdk/include/os32', '-Ifs',
                   '-Ifs/isofs', '-Idrivers/base', '-Idrivers/ata', '-Imm', '-Ilib',
                   '-Iplatform/powermac', 'platform/powermac/root_boot.c', str(d / 'boot_root.c'),
                   '-Wl,--wrap=vfs_register_fs', '-Wl,--wrap=vfs_mount',
                   'drivers/block/ram_ro.c', 'drivers/base/dev.c', 'fs/vfs.c',
                   'fs/isofs/iso9660.c', 'fs/isofs/iso9660_media.c', 'fs/ramfs/ramfs.c',
                   'lib/kstring_c.c', 'tools/tests/boot_root_host.c', '-o', str(d / 'test')]
            subprocess.run(cmd, cwd=ROOT, check=True)
            result = subprocess.run([str(d / 'test'), str(d / 'root.iso')], cwd=ROOT)
            expected = 4 if mutant else 0
            if result.returncode != expected:
                raise RuntimeError(f'exit {result.returncode}, expected {expected}')
            print('RED /tmp mount (exit 4)' if mutant else 'boot root: OK')
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
