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
import keep_tmp

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

    def exchange(self, line):
        self.process.stdin.write((line+'\r').encode())
        return self.wait()

    def command(self, line, ack='ok'):
        # IEEE 1275-1994 lists a 128-character console buffer. CI accepted
        # the 73-byte checksum definition but truncated the longer probe.
        size = len(line.encode('ascii'))
        if size > 73 or not line.isprintable():
            raise ValueError(f'invalid OF input line ({size} bytes): {line!r}')
        print(f'OF line | {size:2} | {ack:8} | {line}', flush=True)
        reply = self.exchange(line)
        print(f'OF {line!r} -> {bytes(reply)!r}', flush=True)
        if re.search(rb'undefined word|exception|stack underflow|stack overflow|not found',
                     reply, re.I):
            raise RuntimeError(f'Open Firmware failed: {line!r}: {reply!r}')
        if not re.search(rb'\b' + ack.encode() + rb'\r?\n0 > $', reply):
            raise RuntimeError(f'Open Firmware did not acknowledge {line!r}: {reply!r}')
        return reply


def firmware(raw, expected, volume_size):
    cmd = ['qemu-system-ppc', '-M', 'mac99', '-cpu', '7450', '-m', '128M', '-nographic',
           '-bios', ROM, '-monitor', 'none', '-serial', 'stdio', '-prom-env', 'auto-boot?=false',
           '-drive', f'file={raw},format=raw,if=ide,index=0,snapshot=on']
    with subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, bufsize=0) as process:
        try:
            s = FirmwareSession(process)
            s.wait()
            return inspect_media(s, expected, volume_size)
        finally:
            process.kill()


def inspect_media(s, expected, volume_size):
    s.command('hex')
    # Read the APM through the whole-disk OF handle. File reads alone
    # can succeed even if the advertised partition ends a sector early.
    s.command('" hd:0" open-dev value drive')
    s.command('200 alloc-mem value sector')
    s.command(': seek-sector 200 * 0 " seek" drive $call-method ;')
    s.command(': read-sector sector 200 " read" drive $call-method ;')
    s.command(': sector@ seek-sector read-sector ;')
    def read_sector(lba):
        result = s.command(f'cr ." IO " {lba:x} sector@ swap . . cr')
        if not re.search(rb'(?m)^IO 0 200 ', result):
            raise RuntimeError(f'firmware sector {lba} read failed: {result!r}')
    read_sector(2)
    # IEEE 1275 l@ fetches an aligned quadlet on this big-endian target.
    result = s.command('cr ." MAP " sector 8 + l@ . '
                       'sector c + l@ . cr')
    match = re.search(rb'(?m)^MAP ([0-9a-fA-F]+) ([0-9a-fA-F]+) ', result)
    if not match:
        raise RuntimeError(f'no firmware partition result: {result!r}')
    first, count = (int(value, 16) for value in match.groups())
    if count * 512 != volume_size:
        return False
    for lba in (first + 2, first + count - 2):
        read_sector(lba)
        result = s.command('cr ." MDB " sector l@ 10 rshift . cr')
        if not re.search(rb'(?m)^MDB 4244 ', result):
            raise RuntimeError(f'invalid firmware MDB: {result!r}')
    s.command('4000 constant chunk 0 value buf 0 value fh 0 value sum32')
    s.command(': checksum sum32 -rot bounds ?do 21 * i c@ + ffffffff and loop to sum32 ;')
    s.command(': read-chunk buf chunk " read" fh $call-method ;')
    s.command(': probe read-chunk dup 0> over chunk <= and', 'compiled')
    s.command('if dup buf swap checksum then cr ." RESULT " . sum32 u. cr ;')
    for name, content in expected.items():
        length = len(content)
        result = s.command(f'" hd:2,\\{name}" open-dev to fh cr ." OPEN " fh 0<> . cr')
        if re.search(rb'(?m)^OPEN 0 ', result):
            return False
        if not re.search(rb'(?m)^OPEN (?:-1|[fF]+) ', result):
            raise RuntimeError(f'no firmware handle result for {name}: {result!r}')
        s.command('chunk alloc-mem to buf 0 to sum32')
        total = 0
        while total <= length:
            result = s.command('probe')
            match = re.search(rb'(?m)^RESULT ([0-9a-fA-F]+) ([0-9a-fA-F]+) ', result)
            if not match:
                raise RuntimeError(f'invalid firmware read for {name}: {result!r}')
            count, checksum = (int(value, 16) for value in match.groups())
            if count > 0x4000:
                raise RuntimeError(f'invalid firmware read length for {name}: {count}')
            total += count
            if count == 0:
                break
        s.command('fh close-dev buf chunk free-mem')
        wanted = 0
        for value in content:
            wanted = (wanted*33 + value) & 0xffffffff
        if (total, checksum) != (length, wanted):
            return False
    return True


class HostSession(FirmwareSession):
    """Model serial replies and media reads for the shared acceptance script."""
    def __init__(self, raw, forks):
        self.raw = raw
        self.forks = forks
        self.compiling = False

    def exchange(self, line):
        words = line.split()
        if words[0] == ':':
            self.compiling = True
        if words[-1] == ';':
            self.compiling = False
        output = ''
        if 'sector@' in words and 'IO' in words:
            offset = int(words[words.index('sector@') - 1], 16) * 512
            self.sector = self.raw[offset:offset + 512]
            output = f'IO 0 {len(self.sector):x} '
        elif 'MAP' in words:
            first, count = struct.unpack_from('>II', self.sector, 8)
            output = f'MAP {first:x} {count:x} '
        elif 'MDB' in words:
            output = f'MDB {integer(self.sector, 0, 2):x} '
        elif 'OPEN' in words:
            name = line.split("\\", 1)[1].split('"', 1)[0]
            self.content = self.forks.get(name)
            self.offset = self.checksum = 0
            output = 'OPEN 0 ' if self.content is None else 'OPEN -1 '
        elif line == 'probe':
            chunk = self.content[self.offset:self.offset + 0x4000]
            self.offset += len(chunk)
            for byte in chunk:
                self.checksum = (33 * self.checksum + byte) & 0xffffffff
            output = f'RESULT {len(chunk):x} {self.checksum:x} '
        ack = 'compiled' if self.compiling else 'ok'
        return f'{line} \r\n{output}\r\n {ack}\r\n0 > '.encode()


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
        cases.extend((case, None) for case in ('media-load', 'short', 'long'))
        for name in FILES:
            cases.extend([(name+'-path', files[name][0]), (name+'-contents', files[name][1])])
    with keep_tmp.temporary_directory() as directory:
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
            assert copy[-1024:-512] == copy[1024:1536]
            if case in ('short', 'long'):
                count += 1 if case == 'long' else -1
                struct.pack_into('>I', raw, 1036, count)
                struct.pack_into('>I', raw, 1108, count)
                if case == 'long':
                    raw.extend(bytes(512))
                    struct.pack_into('>I', raw, 4, len(raw)//512)
            payload = raw[start*512:(start+count)*512]
            entries = catalog(payload)
            forks = {name: payload[start:start+size] for name, (_, start, size) in entries.items()}
            matches = len(payload) == len(copy) and all(forks.get(name) == content for name, content in expected.items())
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
            if args.build_only:
                assert inspect_media(HostSession(raw, forks), expected, len(copy)) == matches
            else:
                assert firmware(path, expected, len(copy)) == matches
            print(case, digest, flush=True)
    print('PASS product install media' + (' (build only)' if args.build_only else ''))


if __name__ == '__main__':
    main()
