#!/usr/bin/env python3
"""Link and package a PowerPC user application with the production CRT."""
CHECK = dict(covers=['sdk/link/ppc/app.ld', 'tools/ppc/test_app_link.py'],
             inputs=['sdk/**', 'tools/ppc/test_app_link.py'])
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BIN = Path('/home/hight/opt/cross-ppc/bin')
sys.path.insert(0, str(ROOT / 'sdk'))
import os32x_hdr as hdr


def call(args):
    return subprocess.run([str(x) for x in args], check=True, text=True)


def exercise(tmp, mutant=False):
    script = tmp / 'app.ld'
    source = (ROOT / 'sdk/link/ppc/app.ld').read_text()
    if mutant:
        assert source.count('0x80100000') == 1
        source = source.replace('0x80100000', '0x80000000')
    script.write_text(source)
    main = tmp / 'main.c'
    main.write_text('#include "os32api.h"\nint initialized = 7; int zeroed;\n'
                    'int main(int argc, char **argv, KernelAPI *api) '
                    '{ (void)argv; (void)api; zeroed = initialized; return argc; }\n')
    note = struct.pack('<4I', hdr.OS32X_HDR_VERSION, hdr.OS32_KAPI_ABI_GENERATION,
                       hdr.OS32_MEMORY_LAYOUT_GENERATION, hdr.OS32_SHLIB_PROTOCOL)
    with main.open('a') as f:
        f.write('__attribute__((used,section(".os32_generations"))) const unsigned char note[] = {' +
                ','.join(str(x) for x in note) + '};\n')
        f.write('__asm__(".pushsection .os32_generation_ref,\\"\\",@progbits\\n.long __os32_abi_1_mem_4_fmt_5\\n.popsection");\n')
    flags = ['-m32', '-mbig-endian', '-mcall-sysv', '-mno-eabi', '-msdata=none',
             '-msoft-float', '-mno-altivec', '-ffreestanding', '-fno-pie',
             '-fno-asynchronous-unwind-tables', '-nostdlib', '-Wall', '-Wextra', '-Werror', '-Wno-attributes',
             '-I' + str(ROOT / 'sdk/include/os32')]
    objs = []
    for i, src in enumerate([ROOT / 'sdk/crt/ppc/crt0.S', ROOT / 'sdk/crt/ppc/start.c', main]):
        obj = tmp / f'{i}.o'
        extra = []
        call([BIN / 'powerpc-elf-gcc', *flags, *extra, '-c', src, '-o', obj])
        objs.append(obj)
    elf = tmp / 'app.elf'
    call([BIN / 'powerpc-elf-ld', '--gc-sections', '-T', script, '-o', elf, *objs])
    parsed = hdr.Elf32(elf)
    base_ok = parsed.e_entry == 0x80100000 and parsed.symbols['_start'] == 0x80100000
    if mutant:
        assert not base_ok, 'base mutation survived'
        return
    assert base_ok
    sections = {s['name']: s for s in parsed.sections}
    text, data, bss = (sections[n] for n in ('.text', '.data', '.bss'))
    assert parsed.symbols['__text_end'] % 4096 == 0
    assert text['addr'] + text['size'] == parsed.symbols['__text_end'] == data['addr']
    assert bss['type'] == hdr.SHT_NOBITS
    assert parsed.symbols['__bss_start'] == bss['addr']
    assert parsed.symbols['__bss_end'] == bss['addr'] + bss['size']
    for name in ('.os32_kapi_layout', '.os32_generations', '.os32_generation_ref'):
        assert sections[name]['size'] and not (sections[name]['flags'] & hdr.SHF_ALLOC)
    raw, output = tmp / 'app.raw', tmp / 'app.bin'
    call([BIN / 'powerpc-elf-objcopy', '-O', 'binary', elf, raw])
    elf.with_suffix('.inputs.json').write_text(json.dumps({'elf_sha256': hashlib.sha256(elf.read_bytes()).hexdigest()}))
    call([sys.executable, '-B', ROOT / 'sdk/mkos32x.py', raw, output, '--elf', elf, '--target', 'ppc-powermac'])
    fields = struct.unpack('<16I', output.read_bytes()[:64])
    assert fields[3] & 0x40 and fields[4] == 0 and fields[10] == 0x80100000


def main():
    with tempfile.TemporaryDirectory() as td:
        exercise(Path(td))
        if '--mutate' in sys.argv:
            exercise(Path(td), True)
    print('PPC app link: OK')


if __name__ == '__main__':
    main()
