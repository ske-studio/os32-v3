"""Independent ELF32 BE wire and Linux user-mode PPC verifier fixture."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

from root_pair import bind_module, build, wire
import lz4.block

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FIX = HERE/'pair_fixture'
WIRE = bytes.fromhex('04001000 00001000 18000000 20000000 00005800 08000000 '
                     '08000000 00005800 08000000 04001000 10000000 11223344 '
                     '00000000 00000000 00000000 00000000 55667788 00000000 00000000')
# CRC computed by shifting each bit LSB first, polynomial 0xedb88320.
CRC = 0x66aa040c


def paired_iso(work, gcc, module):
    # Construct a real ISO independently of the product packaging rules.
    staging = work/'staging'
    staging.mkdir()
    (staging/'module.bin').write_bytes(module)
    iso = work/'input.iso'
    subprocess.run(['xorriso', '-as', 'mkisofs', '--norock', '-no-pad',
                    '--modification-date=2000010100000000', '--set_all_file_dates',
                    '2000010100000000', '-o', iso, staging], check=True)
    before = iso.read_bytes()
    text = (FIX/'bytes.S').read_text().split('.section .pair_high')[0]
    (work/'low.S').write_text(text)
    subprocess.run([gcc, '-c', work/'low.S', '-o', work/'low.o'], check=True)
    script = (FIX/'fixture.ld').read_text().replace('*(.pair_high)', """
        __root_image_start = .;
        BYTE(0)
        . += root_frame_size - 1;
        __root_file_end = .;
        __root_load_end = .;
    """)
    (work/'build.ld').write_text(script)
    elf = work/'paired.elf'
    link = [gcc, '-nostdlib', '-Wl,-T,'+str(work/'build.ld'), str(work/'low.o'), '-o', str(elf)]
    build(iso, elf, link)
    final = elf.read_bytes()
    payload, rec, off, frame = wire(final)
    pair, crc, size, raw_len = struct.unpack_from('<4I', final, rec)
    raw = (work/'root.iso').read_bytes()
    assert len(raw) == raw_len and zlib.crc32(raw) == crc and zlib.crc32(payload) == pair
    assert lz4.block.decompress(final[off:off+size], uncompressed_size=raw_len) == raw
    assert not any(final[off+size:off+frame])
    assert before.count(module) == 1 and before.replace(module, bind_module(module, pair)) == raw
    build(iso, elf, link)
    assert elf.read_bytes() == final


def test(work, mutate):
    prefix = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-'
    gcc = str(prefix)+'gcc'
    def run(args):
        subprocess.run(args, check=True, cwd=work)
    run([gcc, '-c', FIX/'bytes.S', '-o', 'bytes.o'])
    run([str(prefix)+'ld', '-T', FIX/'fixture.ld', 'bytes.o', '-o', 'fixture.elf'])
    image = (work/'fixture.elf').read_bytes()
    actual, rec, off, frame = wire(image)
    assert actual == WIRE and zlib.crc32(actual) == CRC
    assert image[off:off+frame] == b'highLOAD'
    # Header offsets and values from the PPC KMOD v2 wire format.
    mod = bytearray(148)
    struct.pack_into('<4I', mod, 0, 0x444f4d4b, 2, 144, len(mod))
    struct.pack_into('<I', mod, 36, 1)
    struct.pack_into('<I', mod, 136, 20)
    struct.pack_into('<I', mod, 128, zlib.crc32(mod))
    paired = bind_module(mod, CRC)
    result, checksum = struct.unpack_from('<II', paired, 124)
    struct.pack_into('<I', paired, 128, 0)
    assert result == CRC and checksum == zlib.crc32(paired)
    paired_iso(work, gcc, mod)
    source = ROOT/'platform/powermac/module_pair.c'
    digests = set()
    def guest(src, rc):
        run([gcc, '-O2', '-msdata=none', '-ffreestanding', '-nostdinc', '-nostdlib',
             '-I'+str(ROOT/'include'), '-I'+str(ROOT/'lib'), src, FIX/'check.c', 'bytes.o',
             '-Wl,--section-start=.pair_low=0x100000,--section-start=.pair_high=0x580000',
             '-Wl,-Ttext=0x200000,-e,_start', '-o', 'check.elf'])
        binary = work/'check.elf'
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        assert digest not in digests
        digests.add(digest)
        assert subprocess.run(['qemu-ppc', binary]).returncode == rc
        print(f'{src.name} SHA256 {digest}')
    guest(source, 0)
    if mutate:
        for old, new in [('high, frame, record, record_size','frame, high, record, record_size'),
                         ('zeros(state, record_size)', 'crc32_core_update(state, __root_record_start, record_size)'),
                         ('(u32)__kernel_load_mem_end - low','(u32)__kernel_file_end - low')]:
            text = source.read_text()
            assert text.count(old) == 1
            mutant = work/'mutant.c'
            mutant.write_text(text.replace(old, new))
            guest(mutant, 7)
            print('RED PPC', old)
        host = (HERE/'root_pair.py').read_text()
        for old, new in [('high[2], high[4], rec, 16','high[4], high[2], rec, 16'),
                         ('body[offset:offset+16] = bytes(16)', 'pass'),
                         ('fields.extend((va, size, mem))','fields.extend((va, size, size))')]:
            assert host.count(old) == 1
            ns = {'__file__': str(HERE/'root_pair.py')}
            exec(compile(host.replace(old, new), '<host mutant>', 'exec'), ns)
            changed, *_ = ns['wire'](image)
            assert changed != WIRE and zlib.crc32(changed) != CRC
            print('RED host', old)
    print('PASS fixed ELF wire, PPC BE verifier, ISO CRC gate and KMOD pair/CRC')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    with tempfile.TemporaryDirectory() as work:
        test(Path(work), parser.parse_args().mutate)
