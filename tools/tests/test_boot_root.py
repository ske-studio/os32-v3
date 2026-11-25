"""Exercise platform root boot with the production VFS and filesystems."""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CHECK = dict(name='boot-root-host', tier=1, budget=30, needs=['make:all'],
             covers=['build/core.yaml', 'userland/deploy.yaml', 'tools/deploy_manifests.py', 'tools/mkpkg.py', 'tools/mkfat12.py', 'build/packages.yaml', 'sys/kern/init/boot_root.c', 'platform/powermac/root_boot.[ch]',
                     'sys/platform/powermac/root_image.c', 'sys/kern/mm/pgalloc.h', 'tools/tests/boot_root_host.c', 'tools/tests/test_boot_root.py',
                     'sys/include/boot_root.h', 'kernel/dev/ram_ro.[ch]',
                     'sys/dev/base/dev.c', 'kernel/dev/dev.[ch]', 'sys/kern/dev/dev_name.h', 'fs/vfs*', 'fs/isofs/**',
                     'fs/ramfs/**', 'lib/kstring*', 'lib/path.[ch]', 'tools/mkroot_image.py'],
    basis=['PPC_SHELL_DESIGN §3', 'PPC_SHELL_DESIGN §4', 'PPC_SHELL_DESIGN §5', 'PPC_SHELL_DESIGN §6', 'PPC_SHELL_DESIGN §7', 'PPC_SHELL_DESIGN §8', 'PPC_SHELL_DESIGN §11'],
)


def main():
    source = (ROOT / 'sys/kern/init/boot_root.c').read_text()
    anchor = 'return vfs_mount("/tmp", "rd1", "ramfs");'
    if source.count(anchor) != 1:
        raise RuntimeError('mutation anchor is not unique')
    with tempfile.TemporaryDirectory() as tmp:
        d = pathlib.Path(tmp)
        (d / 'motd').write_bytes(b'RAM root!\n!')
        subprocess.run([sys.executable, '-B', 'tools/mkroot_image.py', '--motd', str(d / 'motd'),
                        '--out', str(d / 'root.iso')], cwd=ROOT, check=True)
        fixed_images = []
        for index, names in enumerate([('a', 'b'), ('b', 'a')]):
            stage = d/f'stage-{index}'
            stage.mkdir()
            for name in names:
                path = stage/name
                path.write_text(name)
                os.utime(path, (1000000000 + 86400*index,) * 2)
            iso = d/f'fixed-{index}.iso'
            subprocess.run([sys.executable, '-B', 'tools/mkroot_image.py', '--staging',
                            stage, '--fixed-dates', '--out', iso], cwd=ROOT, check=True)
            fixed_images.append(iso.read_bytes())
        assert fixed_images[0] == fixed_images[1], 'fixed-date ISO depends on staging'
        release = (ROOT / 'sys/platform/powermac/root_image.c').read_text()
        call = 'pgalloc_free_n_owner(LEDGER_OWNER_BOOT, frame_pfn, pages)'
        if release.count(call) != 1:
            raise RuntimeError('frame release mutation anchor is not unique')
        variants = [('normal', source, release, 0)]
        if '--mutate' in sys.argv:
            variants.extend([
                ('/tmp mount', source.replace(anchor, 'return 0;'), release, 4),
                ('release skipped', source, release.replace(call, '((void)frame_pfn, 1)'), 7),
                ('release twice', source, release.replace(call, f'({call} && {call})'), 7),
            ])
        for name, modified, frame_source, expected in variants:
            (d / 'boot_root.c').write_text(modified)
            (d / 'root_image.c').write_text(frame_source)
            cmd = ['gcc-13', '-O1', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                   '-D__cdecl=', '-I.', '-Iinclude', '-Isdk/include/os32', '-Ifs',
                   '-Ifs/isofs', '-Ikernel/dev', '-Idrivers/ata', '-Imm', '-Ilib',
                   '-Iplatform/powermac', 'sys/platform/powermac/root_boot.c', str(d / 'boot_root.c'), str(d / 'root_image.c'),
                   '-Wl,--wrap=ram_ro_register', '-Wl,--wrap=vfs_register_fs', '-Wl,--wrap=vfs_mount',
                   'sys/kern/dev/ram_ro.c', 'sys/kern/dev/dev.c', 'sys/dev/base/dev.c', 'sys/fs/vfs.c',
                   'sys/fs/isofs/iso9660.c', 'sys/fs/isofs/iso9660_media.c', 'sys/fs/ramfs/ramfs.c',
                   'sys/kern/kstring_c.c', 'lib/path.c', 'tools/tests/boot_root_host.c', '-o', str(d / 'test')]
            subprocess.run(cmd, cwd=ROOT, check=True)
            result = subprocess.run([str(d / 'test'), str(d / 'root.iso')], cwd=ROOT)
            if not expected:
                for step in (-2, 0, 1, 3):
                    subprocess.run([str(d / 'test'), str(d / 'root.iso'), str(step)], check=True)
            if result.returncode != expected:
                raise RuntimeError(f'exit {result.returncode}, expected {expected}')
            print(f'RED {name} (exit {expected})' if expected else 'boot root: OK')
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
