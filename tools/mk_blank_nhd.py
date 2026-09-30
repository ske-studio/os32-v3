#!/usr/bin/env python3
"""Create a blank NHD (T98Next) hard disk image: 512B header + all-zero body.

使い捨ての新規インストール先を作るためだけの道具 (票 S3I2-T 2b)。実 ini・
実プロセス・NP21W_DIR には触れず、指定されたパスに 1 つファイルを作る。

ジオメトリは H=8 / S=17 / セクタ長 512 固定で、容量からシリンダ数を
  C = floor(size_mb * 1024 * 1024 / (H * S * SECTOR_SIZE))
と切り下げ、本体を C*H*S*SECTOR_SIZE B ちょうどにする。NP21/W はヘッダの
C*H*S を総セクタ数として扱う (np21w-src `src/fdd/sxsihdd.c`、`sxsi->totals`)
ので、実ファイル長と一致させないと末尾が読めない。C は 1..65535 だけ通す
(同ファイルの `(cylinders == 0) || (cylinders >= 65536)` で open が失敗する)。

ヘッダは NHDHDR (`src/fdd/sxsihdd.h`):
  sig[16] / comment[0x100] / headersize[4] / cylinders[4] / surfaces[2] /
  sectors[2] / sectorsize[2] / reserved[0xe2] = 512B
署名は `sxsihdd.c:13` の `sig_nhd[15] = "T98HDDIMAGE.R0"` を 15B 比較する。
H / S / セクタ長 / ヘッダ長は tools/nhd_deploy.py の定数と同じ値。
"""
import argparse
import os
from pathlib import Path
import struct
import sys

HEADER_SIZE = 512
SIGNATURE = b'T98HDDIMAGE.R0\x00'  # 15 bytes compared by sxsihdd.c
HEADS = 8
SECTORS = 17
SECTOR_SIZE = 512
CYLINDER_MAX = 65535  # sxsihdd.c refuses cylinders == 0 or >= 65536
DEFAULT_SIZE_MB = 200


class NhdError(RuntimeError):
    """Operator-facing diagnostic; never includes file content."""


def cylinders(size_mb):
    """Floor the requested capacity to a whole cylinder and bound it."""
    if type(size_mb) is not int or isinstance(size_mb, bool):
        raise ValueError('integer size in MB required')
    count = size_mb * 1024 * 1024 // (HEADS * SECTORS * SECTOR_SIZE)
    if not 1 <= count <= CYLINDER_MAX:
        raise ValueError('cylinder count %d outside the 1..%d NP21/W range'
                         % (count, CYLINDER_MAX))
    return count


def header(count):
    """The fixed 512B NHDHDR for this geometry; every other byte is zero."""
    raw = bytearray(HEADER_SIZE)
    raw[:len(SIGNATURE)] = SIGNATURE
    struct.pack_into('<IIHHH', raw, 0x110, HEADER_SIZE, count, HEADS, SECTORS, SECTOR_SIZE)
    return bytes(raw)


def capacity(count):
    return count * HEADS * SECTORS * SECTOR_SIZE


def create(out, size_mb=DEFAULT_SIZE_MB, force=False):
    """Write <out> and return the cylinder count. Refuse symlinks; without
    --force refuse an existing destination instead of overwriting it."""
    count = cylinders(size_mb)
    flags = os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW
    flags |= os.O_TRUNC if force else os.O_EXCL
    try:
        handle = os.open(str(Path(out)), flags, 0o600)
    except FileExistsError:
        raise NhdError('destination exists; pass --force to replace it') from None
    except OSError as exc:
        raise NhdError('destination unavailable, a symlink or not writable') from exc
    try:
        with os.fdopen(handle, 'wb') as stream:
            stream.write(header(count))
            stream.flush()
            # 本体は全ゼロ。truncate なら疎ファイルとして一瞬で作れ、読み出しは
            # どこもゼロになる。
            os.ftruncate(stream.fileno(), HEADER_SIZE + capacity(count))
            os.fsync(stream.fileno())
    except OSError as exc:
        raise NhdError('write failed; the partial image is left for inspection') from exc
    return count


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0], allow_abbrev=False)
    parser.add_argument('--out', required=True, help='destination image path')
    parser.add_argument('--size-mb', type=int, default=DEFAULT_SIZE_MB,
                        help='capacity in MB, floored to a whole cylinder (default %d)'
                             % DEFAULT_SIZE_MB)
    parser.add_argument('--force', action='store_true', help='replace an existing file')
    args = parser.parse_args(argv)
    try:
        count = create(args.out, args.size_mb, args.force)
    except (ValueError, NhdError) as exc:
        print('error: ' + str(exc), file=sys.stderr)
        return 2
    print('C=%d capacity=%d' % (count, capacity(count)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
