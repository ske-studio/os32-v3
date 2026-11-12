#!/usr/bin/env python3
"""Build a plain ISO9660 root image using an installed ISO tool."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--staging', type=Path)
    source.add_argument('--motd', type=Path)
    parser.add_argument('--bin', type=Path, action='append', default=[])
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.bin and args.motd is None:
        parser.error("--bin requires --motd")
    tool = shutil.which('genisoimage') or shutil.which('xorriso')
    if tool is None:
        print('SKIP: genisoimage or xorriso required', file=sys.stderr)
        return 3

    def create(staging):
        cmd = [tool]
        if Path(tool).name == 'xorriso':
            cmd += ['-as', 'mkisofs', '--norock']
        cmd += ['-iso-level', '1', '-no-pad', '-o', str(args.out.resolve()), str(staging)]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        size = args.out.stat().st_size
        if size % 2048:
            raise ValueError('ISO size is not a multiple of 2048')
        print(size)

    try:
        if args.motd is None:
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
