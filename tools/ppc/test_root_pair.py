"""Independent ELF32 BE wire and Linux user-mode PPC verifier fixture."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
from unittest.mock import patch
import tempfile
import zlib

from root_lz4_layout import STAGING_BASE
import root_pair
from root_pair import bind_module, build, wire
import lz4.block

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FIX = HERE/'pair_fixture'
WIRE = bytes.fromhex('04001000 00001000 18000000 20000000 {high} 08000000 '
                     '08000000 {high} 08000000 04001000 10000000 11223344 '
                     '00000000 00000000 00000000 00000000 55667788 00000000 00000000'.format(high=struct.pack('<I', STAGING_BASE).hex()))
# CRC computed by shifting each bit LSB first, polynomial 0xedb88320.
CRC = zlib.crc32(WIRE)


def paired_iso(work, gcc, module):
    # Construct a real ISO independently of the product packaging rules.
    inputs = []
    for index, names in enumerate([('module.bin', 'motd'), ('motd', 'module.bin')]):
        staging = work/f'staging-{index}'
        staging.mkdir()
        for name in names:
            dest = staging/name
            dest.write_bytes(module if name == 'module.bin' else b'hello')
            os.utime(dest, (1000000000 + index*86400,) * 2)
        iso = work/f'input-{index}.iso'
        subprocess.run(['xorriso', '-as', 'mkisofs', '--norock', '-no-pad',
                        '--modification-date=2000010100000000', '--set_all_file_dates',
                        '2000010100000000', '-o', iso, staging], check=True)
        inputs.append(iso.read_bytes())
    assert inputs[0] == inputs[1]
    before = iso.read_bytes()
    text = (FIX/'bytes.S').read_text().split('.section .pair_high')[0]
    (work/'low.S').write_text(text)
    subprocess.run([gcc, f'-DPAIR_CRC={CRC}', '-c', work/'low.S', '-o', work/'low.o'], check=True)
    script = (FIX/'fixture.ld').read_text().replace('*(.pair_high)', """
        __root_image_start = .;
        BYTE(0)
        . += root_frame_size - 1;
        __root_file_end = .;
        __root_load_end = .;
    """)
    (work/'build.ld').write_text(script)
    elf = work/'paired.elf'
    link = [gcc, '-nostdlib', f'-Wl,--defsym,staging_base={STAGING_BASE}', '-Wl,-T,'+str(work/'build.ld'), str(work/'low.o'), '-o', str(elf)]
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
    replace = Path.replace
    def fail_elf_publish(path, target):
        if target == elf:
            raise OSError('ELF publish failed')
        return replace(path, target)
    with patch.object(Path, 'replace', fail_elf_publish):
        try:
            build(iso, elf, link)
        except OSError as error:
            assert str(error) == 'ELF publish failed' and not elf.exists()
        else:
            raise AssertionError('expected ELF publication failure')

    # Model a final compression that crosses the first frame boundary.
    encoder = root_pair.compress
    attempts = []
    def overflow_once(data):
        attempts.append(bytes(data))
        block = encoder(data)
        return block.ljust(frame + 1, b'x') if len(attempts) == 2 else block
    with patch.object(root_pair, 'compress', overflow_once):
        build(iso, elf, link)
    rebuilt = elf.read_bytes()
    payload, rec, off, bigger = wire(rebuilt)
    new_pair, crc, packed, length = struct.unpack_from('<4I', rebuilt, rec)
    assert bigger == root_pair.round_page(frame + 1) + root_pair.PAGE
    assert new_pair == zlib.crc32(payload) and new_pair != pair
    rebound = before.replace(module, bind_module(module, new_pair))
    assert attempts == [before.replace(module, bind_module(module, 0)), raw, rebound]
    assert (work/'root.iso').read_bytes() == rebound and zlib.crc32(rebound) == crc
    assert lz4.block.decompress(rebuilt[off:off+packed], uncompressed_size=length) == rebound



def make_pair_recovery(work):
    # Exercise the production grouped rule with a counting stand-in for the linker.
    out = work/'make-pair'
    out.mkdir()
    (out/'kernel.ld').touch()
    elf = out/'kernel.elf'
    iso = out/'root.iso'
    makefile = out/'Makefile'
    makefile.write_text(f"""KB_OUT := {out}
KB_ELF := {elf}
KB_PAIR_OUTPUT := {iso}
KB_LINK_COMMAND = touch $(KB_ELF) $(KB_PAIR_OUTPUT) && echo link >> {out}/calls
include build/ppc-root-pair.mk
""")
    calls = out/'calls'
    for missing, count in [(None, 1), (None, 1), (iso, 2), (elf, 3)]:
        if missing:
            missing.unlink()
        subprocess.run(['make', '-f', makefile, iso if missing == elf else elf], cwd=ROOT, check=True)
        assert elf.exists() and iso.exists() and calls.read_text().splitlines() == ['link'] * count


def root_image_failures(work, mutate):
    source = (ROOT/'sys/platform/powermac/root_image.c').read_text()
    variants = [('normal', source, 0)]
    if mutate:
        for name, old, new, status in [
            ('allocation', 'return -2;', 'return -1;', 10),
            ('short output', '== (int)length', '>= 0', 12),
            ('data CRC', 'if (crc == le32_rd(__root_record_start + 4))', 'if (1)', 13),
        ]:
            assert source.count(old) == 1
            variants.append((name, source.replace(old, new), status))
    for name, body, status in variants:
        src = work/'root_image.c'
        src.write_text(body)
        binary = work/'root-test'
        subprocess.run(['gcc', '-std=gnu11', '-O0', '-no-pie', '-ffunction-sections',
                        '-Wl,--gc-sections', '-D__cdecl=', '-I.', '-Iinclude', '-Ilib',
                        '-Imm', '-Iplatform/powermac', '-Isdk/include/os32', src,
                        'tools/tests/ppc_root_image_host.c', 'lib/crc32.c', 'lib/lz4.c',
                        '-o', binary], cwd=ROOT, check=True)
        result = subprocess.run([binary])
        assert result.returncode == status, (name, result.returncode, status)
        if status:
            print('RED root image', name)


def test(work, mutate):
    prefix = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-'
    gcc = str(prefix)+'gcc'
    def run(args):
        subprocess.run(args, check=True, cwd=work)
    run([gcc, f'-DPAIR_CRC={CRC}', '-c', FIX/'bytes.S', '-o', 'bytes.o'])
    run([str(prefix)+'ld', '--defsym', f'staging_base={STAGING_BASE}', '-T', FIX/'fixture.ld', 'bytes.o', '-o', 'fixture.elf'])
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
    make_pair_recovery(work)
    root_image_failures(work, mutate)
    source = ROOT/'sys/platform/powermac/module_pair.c'
    digests = set()
    def guest(src, rc, fixture='bytes.o', *flags):
        run([gcc, '-O2', '-msdata=none', '-ffreestanding', '-nostdinc', '-nostdlib',
             '-I'+str(ROOT/'include'), '-I'+str(ROOT/'lib'), src, FIX/'check.c', fixture, *flags,
             f'-DPAIR_CRC={CRC}U',
             f'-Wl,--section-start=.pair_low=0x100000,--section-start=.pair_high={STAGING_BASE:#x}',
             '-Wl,-Ttext=0x200000,-e,_start', '-o', 'check.elf'])
        binary = work/'check.elf'
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        assert digest not in digests
        digests.add(digest)
        assert subprocess.run(['qemu-ppc', binary]).returncode == rc
        print(f'{src.name} SHA256 {digest}')
    guest(source, 0)
    # Keep the recorded pair but substitute a different low LOAD.
    foreign = work/'foreign.S'
    foreign.write_text((FIX/'bytes.S').read_text().replace('0x55667788', '0x55667789'))
    run([gcc, f'-DPAIR_CRC={CRC}', '-c', foreign, '-o', 'foreign.o'])
    guest(source, 0, 'foreign.o', '-DEXPECT_FOREIGN')
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
