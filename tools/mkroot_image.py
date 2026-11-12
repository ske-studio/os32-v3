#!/usr/bin/env python3
"""Build a plain ISO9660 root image using an installed ISO tool."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import mkpkg
import mkfat12

FD_MIN_FREE_KB = 64


def fd_free_bytes(geometry, iso_size, loader_size):
    mkfat12.select_geometry(geometry)
    unit = mkfat12.BYTES_PER_SECTOR * mkfat12.SECTORS_PER_CLUSTER
    rounded = lambda n: (n + unit - 1) // unit * unit
    return mkfat12.TOTAL_DATA_CLUSTERS * unit - rounded(iso_size) - rounded(loader_size)


# ISO 9660 level 1 d-characters: anything else would be renamed by the tool.
_D_NAME = __import__('re').compile(r'[A-Z0-9_]{1,8}(\.[A-Z0-9_]{0,3})?')


def check_collision(target, seen):
    for part in target.split('/'):
        if not _D_NAME.fullmatch(part):
            raise ValueError(f'not an ISO 9660 level 1 name: {target}')
    if target in seen:
        raise ValueError(f'ISO name collision: {target}')
    seen.add(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--staging', type=Path)
    source.add_argument('--motd', type=Path)
    source.add_argument('--plan', type=Path)
    parser.add_argument('--fd', action='store_true')
    parser.add_argument('--base', type=Path, default=Path('.'))
    parser.add_argument('--bin', type=Path, action='append', default=[])
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.bin and args.motd is None:
        parser.error("--bin requires --motd")
    if bool(args.plan) != args.fd:
        parser.error('--plan and --fd must be used together')
    tool = shutil.which('genisoimage') or shutil.which('xorriso')
    if tool is None:
        print(('ERROR' if args.plan else 'SKIP') + ': genisoimage or xorriso required', file=sys.stderr)
        return 1 if args.plan else 3

    def create(staging):
        cmd = [tool]
        if Path(tool).name == 'xorriso':
            cmd += ['-as', 'mkisofs', '--norock']
        cmd += ['-iso-level', '1', '-no-pad', '-o', str(args.out.resolve()), str(staging)]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        size = args.out.stat().st_size
        if size % 2048:
            raise ValueError('ISO size is not a multiple of 2048')
        if args.plan:
            sizes = [p.stat().st_size for p in staging.rglob('*') if p.is_file()]
            print(f'ISO: {size} bytes; contents: {sum(sizes)} bytes; '
                  f'2048-rounded contents: {sum((n + 2047) // 2048 * 2048 for n in sizes)} bytes')
        else:
            print(size)

    try:
        if args.plan:
            files, problems = mkpkg.fd_plan(mkpkg.load_plan(args.plan), str(args.base),
                                           loader='boot/loader_fat_new.bin')
            if problems:
                raise ValueError('; '.join(problems))
            with tempfile.TemporaryDirectory(prefix='os32-root-') as tmp:
                staging = Path(tmp)
                seen = set()
                for guest, host in files:
                    if host == 'boot/loader_fat_new.bin':
                        continue
                    target = guest.strip('/').upper()
                    check_collision(target, seen)
                    dest = staging / target
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(args.base / host, dest)
                create(staging)
        elif args.motd is None:
            create(args.staging)
        else:
            motd = args.motd.read_bytes()
            if len(motd) > 1024 or not motd.isascii():
                raise ValueError('motd must be ASCII and at most 1024 bytes')
            with tempfile.TemporaryDirectory(prefix='os32-root-') as tmp:
                staging = Path(tmp)
                (staging / 'etc').mkdir()
                (staging / 'tmp').mkdir()
                (staging / 'etc' / 'motd').write_bytes(motd)
                if args.bin:
                    (staging / 'bin').mkdir()
                    for app in args.bin:
                        shutil.copyfile(app, staging / 'bin' / app.name)
                create(staging)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f'mkroot_image: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
