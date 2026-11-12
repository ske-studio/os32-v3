#!/usr/bin/env python3
"""Exercise PPC ELF packaging and the endian mutation."""
CHECK = dict(covers=['sdk/os32x_hdr.py', 'sdk/mkos32x.py', 'tools/ppc/test_os32x_endian.py'],
             inputs=['sdk/**', 'tools/ppc/test_os32x_endian.py'])
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PPC = Path('/home/hight/opt/cross-ppc/bin')


def call(args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, capture_output=True, **kwargs)


def exercise(tmp, sdk, mutant=False):
    src = tmp / 'app.c'
    src.write_text('__attribute__((section(".text.startup"))) void start(void) {}\n'
                   'int data = 7; int bss;\n'
                   '__attribute__((section(".os32_kapi_layout"),used)) const unsigned stamp=0x1234;\n'
                   '__attribute__((section(".os32_generations"),used)) const unsigned char note[] = {' +
                   ','.join(str(x) for x in struct.pack('<4I', 5, 1, 1, 2)) + '};\n')
    # Obtain the current generation values instead of assuming their versions.
    sys.path.insert(0, str(ROOT / 'sdk'))
    import os32x_hdr as h
    note = struct.pack('<4I', h.OS32X_HDR_VERSION, h.OS32_KAPI_ABI_GENERATION,
                       h.OS32_MEMORY_LAYOUT_GENERATION, h.OS32_SHLIB_PROTOCOL)
    s = src.read_text(); s = s[:s.index('__attribute__((section(".os32_generations"),used))')]
    src.write_text(s + '__attribute__((section(".os32_generations"),used)) const unsigned char note[] = {' + ','.join(str(x) for x in note) + '};\n')
    script = tmp / 'app.ld'
    script.write_text('ENTRY(start)\nSECTIONS { . = 0x80100000; .text : { *(.text*) . = ALIGN(4096); } __text_end = .; .data : { *(.data*) . = ALIGN(4); } __bss_start = .; .bss : { *(.bss*) *(COMMON) } __bss_end = .; .os32_kapi_layout 0 (INFO) : { *(.os32_kapi_layout) } .os32_generations 0 (INFO) : { *(.os32_generations) } /DISCARD/ : { *(.comment) *(.eh_frame*) } }\n')
    elf = tmp / 'app.elf'; raw = tmp / 'app.raw'; out = tmp / 'app.bin'
    call([PPC / 'powerpc-elf-gcc', '-m32', '-mbig-endian', '-msoft-float', '-msdata=none', '-ffreestanding', '-fno-pie', '-fno-asynchronous-unwind-tables', '-nostdlib', '-c', src, '-o', tmp / 'app.o'])
    call([PPC / 'powerpc-elf-ld', '-T', script, '-o', elf, tmp / 'app.o'])
    call([PPC / 'powerpc-elf-objcopy', '-O', 'binary', elf, raw])
    elf.with_suffix('.inputs.json').write_text(json.dumps({'elf_sha256': hashlib.sha256(elf.read_bytes()).hexdigest()}))
    cmd = [sys.executable, '-B', sdk / 'mkos32x.py', raw, out, '--elf', elf, '--target', 'ppc-powermac']
    result = subprocess.run([str(x) for x in cmd], capture_output=True, text=True)
    if mutant:
        assert result.returncode != 0, 'endian mutant survived'
        return
    assert result.returncode == 0, result.stderr
    fields = struct.unpack('<16I', out.read_bytes()[:64])
    assert fields[3] & 0x40 and fields[4] == 0 and fields[10] == 0x80100000 and fields[11] == 0x1234
    assert out.read_bytes()[64:] == raw.read_bytes()
    x86 = tmp / 'x86.elf'
    call(['/home/hight/opt/cross/bin/i386-elf-gcc', '-m32', '-ffreestanding', '-nostdlib', '-c', src, '-o', x86])
    cmd[cmd.index(elf)] = x86
    result = subprocess.run([str(x) for x in cmd], capture_output=True, text=True)
    assert result.returncode != 0 and 'ELF target mismatch' in result.stderr
    cmd[cmd.index(x86)] = elf
    cmd[-1] = 'x86-pc98'
    result = subprocess.run([str(x) for x in cmd], capture_output=True, text=True)
    assert result.returncode != 0 and 'ELF target mismatch' in result.stderr


def main():
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        exercise(tmp, ROOT / 'sdk')
        if '--mutate' in sys.argv:
            sdk = tmp / 'sdk'; shutil.copytree(ROOT / 'sdk', sdk)
            path = sdk / 'os32x_hdr.py'; s = path.read_text()
            old = "self.endian = '<' if d[5] == 1 else '>'"
            assert s.count(old) == 1
            path.write_text(s.replace(old, "self.endian = '>' if d[5] == 1 else '<'"))
            exercise(tmp, sdk, True)
    print('PPC ELF packaging: OK')


if __name__ == '__main__':
    main()
