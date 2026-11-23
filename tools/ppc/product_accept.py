"""Inspect both classic HFS data forks, then read them through Open Firmware."""
import argparse
import hashlib
import os
import select
import time
import re
from pathlib import Path
import struct
import subprocess
import tempfile

from product_media import build_volume, product_load
from lan_probe import PROMPT, ROM

FILES = ('LOADER.ELF', 'PRODUCT.ELF')
ROOT = Path(__file__).resolve().parents[2]


def integer(data, pos, size):
    return int.from_bytes(data[pos:pos+size], 'big')


def catalog(volume):
    mdb = 1024
    if volume[mdb:mdb+2] != b'BD':
        raise ValueError('classic HFS signature')
    block_size = integer(volume, mdb+20, 4)
    base = integer(volume, mdb+28, 2)*512

    def extent(record, at):
        return base + integer(record, at, 2)*block_size

    tree = extent(volume, mdb+150)
    node_size = integer(volume, tree+32, 2)
    node = integer(volume, tree+24, 4)
    files = {}
    seen = set()
    while node:
        if node in seen or node_size < 512 or tree+(node+1)*node_size > len(volume):
            raise ValueError('invalid HFS catalog chain')
        seen.add(node)
        start = tree + node*node_size
        if volume[start+8] != 255:
            raise ValueError('expected catalog leaf')
        for i in range(integer(volume, start+10, 2)):
            key = start+integer(volume, start+node_size-2*(i+1), 2)
            record = key + ((volume[key]+2) & ~1)
            if integer(volume, key+2, 4) != 2 or volume[record] != 2:
                continue
            name = volume[key+7:key+7+volume[key+6]].decode('mac_roman')
            size = integer(volume, record+26, 4)
            offset = extent(volume, record+74)
            if size > integer(volume, record+76, 2)*block_size or offset+size > len(volume):
                raise ValueError('invalid data fork')
            files[name] = (key+7, offset, size)
        node = integer(volume, start, 4)
    return files


def disk(volume):
    start = 64
    sectors = len(volume)//512
    data = bytearray(start*512) + volume
    struct.pack_into('>HHI', data, 0, 0x4552, 512, start+sectors)
    for idx, (first, length, kind) in enumerate(((1, 63, b'Apple_partition_map'),
                                                 (start, sectors, b'Apple_HFS')), 1):
        off = idx*512
        struct.pack_into('>HHIII', data, off, 0x504d, 0, 2, first, length)
        data[off+48:off+48+len(kind)] = kind
        struct.pack_into('>III', data, off+80, 0, length, 0x33)
    return data


class FirmwareSession:
    def __init__(self, process):
        self.process = process

    def wait(self):
        reply = bytearray()
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            if select.select([self.process.stdout], [], [], 1)[0]:
                data = os.read(self.process.stdout.fileno(), 65536)
                if not data:
                    raise RuntimeError("Open Firmware closed the serial port")
                reply.extend(data)
                if PROMPT.search(reply):
                    return reply
        raise TimeoutError("Open Firmware prompt")

    def command(self, line):
        self.process.stdin.write((line+'\r').encode())
        return self.wait()


def firmware(raw, expected):
    cmd = ['qemu-system-ppc', '-M', 'mac99', '-cpu', '7450', '-m', '128M', '-nographic',
           '-bios', ROM, '-monitor', 'none', '-serial', 'stdio', '-prom-env', 'auto-boot?=false',
           '-drive', f'file={raw},format=raw,if=ide,index=0,snapshot=on']
    with subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, bufsize=0) as process:
        try:
            s = FirmwareSession(process)
            s.wait()
            s.command('hex')
            s.command(': checksum 0 -rot bounds ?do 21 * i c@ + ffffffff and loop ;')
            for name, content in expected.items():
                length = len(content)
                s.command(f'{length:x} alloc-mem value buf')
                s.command(f'" hd:2,\\{name}" open-dev value fh')
                result = s.command(f': probe cr ." RESULT " fh if buf {length:x} " read" '
                                   f'fh $call-method . buf {length:x} checksum . '
                                   'else ." MISSING" then cr ; probe')
                match = re.search(rb'(?m)^RESULT (MISSING|[0-9a-fA-F ]+)[\r\n]', result)
                if not match:
                    raise RuntimeError(f'no firmware result for {name}: {result!r}')
                wanted = 0
                for value in content:
                    wanted = (wanted*33 + value) & 0xffffffff
                if match[1] == b'MISSING' or [int(x, 16) for x in match[1].split()] != [length, wanted]:
                    return False
            return True
        finally:
            process.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    out = ROOT/'build/out/ppc-powermac'
    subprocess.run(['make', '-j6', 'ARCH=ppc', 'PLATFORM=powermac', 'IMAGE=powermac', 'iso'],
                   cwd=ROOT, check=True)
    def extract(name):
        return subprocess.check_output(['isoinfo', '-i', out/'boot.iso', '-x', '/'+name+';1'])
    for name, source in [('ROOT.ISO', out/'root.iso'), ('PPCKERN.ELF', out/'PPCKERN.ELF')]:
        assert extract(name) == source.read_bytes()
    expected = {name: extract(name) for name in FILES}
    for name, content in expected.items():
        assert content == (out/'hdd'/name).read_bytes()
    product_load(expected['PRODUCT.ELF'])
    volume = extract('BOOT.HFS')
    assert volume == (out/'hdd/BOOT.HFS').read_bytes() and len(volume)%512 == 0
    files = catalog(volume)
    cases = [('normal', None)]
    if args.mutate:
        cases.append(('media-load', None))
        for name in FILES:
            cases.extend([(name+'-path', files[name][0]), (name+'-contents', files[name][1])])
    with tempfile.TemporaryDirectory() as directory:
        hashes = set()
        for case, offset in cases:
            copy = bytearray(volume)
            if offset is not None:
                copy[offset] ^= 1
            if case == 'media-load':
                replacement = Path(directory)/'PRODUCT.ELF'
                replacement.write_bytes(extract('PPCKERN.ELF'))
                hfs = Path(directory)/'media.hfs'
                build_volume(hfs, out/'hdd/LOADER.ELF', replacement)
                copy = hfs.read_bytes()
            raw = disk(copy)
            start, count = struct.unpack_from('>II', raw, 1032)
            assert start*512+count*512 == len(raw) and count*512 == len(copy)
            payload = raw[start*512:]
            entries = catalog(payload)
            forks = {name: payload[start:start+size] for name, (_, start, size) in entries.items()}
            matches = all(forks.get(name) == content for name, content in expected.items())
            assert matches == (case == 'normal'), case
            if case in ('normal', 'media-load'):
                try:
                    product_load(forks['PRODUCT.ELF'])
                except ValueError:
                    assert case == 'media-load'
                else:
                    assert case == 'normal', 'embedded media LOAD accepted'
            digest = hashlib.sha256(raw).hexdigest()
            assert digest not in hashes
            hashes.add(digest)
            path = Path(directory)/(case+'.raw')
            path.write_bytes(raw)
            if not args.build_only:
                assert firmware(path, expected) == matches
            print(case, digest, flush=True)
    print('PASS product install media' + (' (build only)' if args.build_only else ''))


if __name__ == '__main__':
    main()
