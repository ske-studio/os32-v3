#!/usr/bin/env python3
"""Write FD-only network CD settings to a separate raw FAT12 floppy image.

Example: fd_netcd_config.py boot.img --output netcd.img --netcd install
         --peer 02:00:00:00:00:01
Uses mkfat12's FAT, allocation and directory writer for OS32 2HD/144 images.
"""
import argparse
from pathlib import Path
import re
import struct

import mkfat12 as fat

CONFIG_PATH = '/etc/netcd.cfg'


class Floppy(fat.Fat12Builder):
    def __init__(self, image):
        for name in fat.GEOMETRIES:
            fat.select_geometry(name)
            if (len(image) == fat.TOTAL_SECTORS * fat.BYTES_PER_SECTOR
                    and image[11:36] == fat.make_bpb()[11:36]):
                break
        else:
            raise ValueError('expected an OS32 raw 2HD or 1.44MB FAT12 image')
        super().__init__()
        self.image[:] = image
        start = fat.FAT_START * fat.BYTES_PER_SECTOR
        self.fat[:] = image[start:start + len(self.fat)]
        start = fat.ROOT_DIR_START * fat.BYTES_PER_SECTOR
        self.root_dir[:] = image[start:start + len(self.root_dir)]
        self.root_dir_index = self.end_index(self.root_dir)
        etc = self._find_dir_in_parent(0, fat.name_to_83('etc'))
        if etc is not None:
            chain = self.chain(etc)
            data = bytearray(self.read_chain(etc))
            self.subdir_info[etc] = dict(data=data, index=self.end_index(data),
                                        last_cluster=chain[-1])

    @staticmethod
    def end_index(data):
        return next((i // 32 for i in range(0, len(data), 32) if data[i] == 0),
                    len(data) // 32)

    def chain(self, cluster):
        result = []
        while cluster < 0xFF8:
            if not 2 <= cluster < fat.TOTAL_DATA_CLUSTERS + 2 or cluster in result:
                raise ValueError('invalid FAT12 cluster chain')
            result.append(cluster)
            cluster = fat.fat12_get(self.fat, cluster)
        return result

    def read_chain(self, cluster):
        size = fat.BYTES_PER_SECTOR * fat.SECTORS_PER_CLUSTER
        return b''.join(self.image[self._cluster_to_offset(c):
                                  self._cluster_to_offset(c) + size]
                        for c in self.chain(cluster))

    def _alloc_cluster(self):
        # Existing images need free-cluster allocation rather than append-only.
        for cluster in range(2, fat.TOTAL_DATA_CLUSTERS + 2):
            if fat.fat12_get(self.fat, cluster) == fat.FAT12_FREE:
                return cluster
        raise RuntimeError('floppy is full')

    def config_entry(self):
        etc = self._find_dir_in_parent(0, fat.name_to_83('etc'))
        if etc is None:
            return None
        data = self.subdir_info[etc]['data']
        for i in range(0, self.end_index(data) * 32, 32):
            if data[i:i + 11] == fat.name_to_83('netcd.cfg'):
                if data[i + 11] & 0x18:
                    raise ValueError('netcd.cfg is not a regular file')
                return etc, i
        return None

    def read_config(self):
        entry = self.config_entry()
        if entry is None:
            raise ValueError('missing ' + CONFIG_PATH)
        etc, index = entry
        data = self.subdir_info[etc]['data']
        cluster, size = struct.unpack_from('<HI', data, index + 26)
        return self.read_chain(cluster)[:size] if size else b''

    def write_config(self, payload):
        old = self.config_entry()
        etc = self._ensure_directory('/etc')
        if old is not None:
            _, index = old
            info = self.subdir_info[etc]
            cluster = struct.unpack_from('<H', info['data'], index + 26)[0]
            if cluster:
                for c in self.chain(cluster):
                    fat.fat12_set(self.fat, c, fat.FAT12_FREE)
            info['index'] = index // fat.DIR_ENTRY_SIZE
        cluster = self._write_file_data(payload)
        self._add_subdir_entry(etc, fat.name_to_83('netcd.cfg'), 0x20,
                               cluster, len(payload))
        # Preserve the IPL and BPB byte-for-byte; only flush FAT/directories.
        boot = bytes(self.image[:fat.BYTES_PER_SECTOR])
        result = bytearray(self.build())
        result[:len(boot)] = boot
        return bytes(result)


def configure(source, output, alias, peer):
    if not re.fullmatch(r'[A-Za-z0-9_.-]{1,63}', alias):
        raise ValueError('netcd alias must be 1..63 ASCII letters/digits/_.-')
    if not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', peer):
        raise ValueError('peer must be a colon-separated MAC address')
    if source.resolve() == output.resolve() or (output.exists() and source.samefile(output)):
        raise ValueError('input and output must be different images')
    payload = f'NETCD={alias}\nSFS_LAN=1\nSFS_LAN_PEER={peer.lower()}\n'.encode('ascii')
    result = Floppy(source.read_bytes()).write_config(payload)
    with output.open('xb') as stream:
        stream.write(result)
    if Floppy(output.read_bytes()).read_config() != payload:
        raise ValueError('netcd.cfg readback mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--netcd', required=True)
    parser.add_argument('--peer', required=True)
    args = parser.parse_args()
    try:
        configure(args.image, args.output, args.netcd, args.peer)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f'fd_netcd_config: {error}\n')
    print(f'{args.output}: {CONFIG_PATH} written and verified')


if __name__ == '__main__':
    main()
