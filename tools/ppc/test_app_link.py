#!/usr/bin/env python3
"""Link and package a PowerPC user application with the production CRT."""
CHECK = dict(covers=['sdk/link/ppc/app.ld', 'tools/ppc/test_app_link.py'],
             inputs=['sdk/**', 'tools/ppc/test_app_link.py'])
from pathlib import Path
import struct
import shutil
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
    flags = ['-m32', '-mbig-endian', '-mcall-sysv', '-mno-eabi', '-msdata=none',
             '-msoft-float', '-mno-altivec', '-ffreestanding', '-fno-pie',
             '-fno-asynchronous-unwind-tables', '-nostdlib', '-Wall', '-Wextra', '-Werror', '-Wno-attributes',
             '-I' + str(ROOT / 'sdk/include/os32'), '-include', str(ROOT / 'sdk/include/os32/os32_unit_stamp.h')]
    asm = tmp / 'crt0.S'
    asm.write_text((ROOT / 'sdk/crt/ppc/crt0.S').read_text() +
                   '\n.section .os32_generations,"",@progbits\n.long %d,%d,%d,%d\n' %
                   (hdr.OS32X_HDR_VERSION, hdr.OS32_KAPI_ABI_GENERATION,
                    hdr.OS32_MEMORY_LAYOUT_GENERATION, hdr.OS32_SHLIB_PROTOCOL))
    objs = []
    for i, src in enumerate([asm, ROOT / 'sdk/crt/ppc/start.c', main]):
        obj = tmp / f'{i}.o'
        unit_flags = flags[:-2] if i == 0 else flags
        call([BIN / 'powerpc-elf-gcc', *unit_flags, '-c', src, '-o', obj])
        objs.append(obj)
    elf = tmp / 'app.elf'
    command = [sys.executable, '-B', ROOT / 'sdk/link_guard.py', '--target', 'ppc-powermac',
               BIN / 'powerpc-elf-ld', '--gc-sections', '-T', script, '-o', elf, *objs]
    call(command)
    assert elf.with_suffix('.inputs.json').is_file()
    if not mutant:
        for name, source, diagnostic in [
                ('missing', 'int missing_unit;\n', 'missing per-unit generations'),
                ('wrong', '__attribute__((used,section(".os32_generations"))) const unsigned int bad[] = {0,0,0,0};\n', 'incompatible per-unit generations')]:
            src = tmp / (name + '.c'); src.write_text(source)
            obj = tmp / (name + '.o')
            call([BIN / 'powerpc-elf-gcc', *flags[:-2], '-c', src, '-o', obj])
            result = subprocess.run([str(x) for x in [*command, obj]], capture_output=True, text=True)
            assert result.returncode != 0 and diagnostic in result.stderr, result.stderr
        result = subprocess.run([str(x) for x in [*command, '-lgcc']], capture_output=True, text=True)
        assert result.returncode != 0 and 'does not support' in result.stderr
        call(command)
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
    call([sys.executable, '-B', ROOT / 'sdk/mkos32x.py', raw, output, '--elf', elf, '--target', 'ppc-powermac'])
    fields = struct.unpack('<16I', output.read_bytes()[:64])
    assert fields[3] & 0x40 and fields[4] == 0 and fields[10] == 0x80100000


def main():
    with tempfile.TemporaryDirectory() as td:
        exercise(Path(td))
        if '--mutate' in sys.argv:
            exercise(Path(td), True)
            (Path(td) / 'app.ld').write_text((ROOT / 'sdk/link/ppc/app.ld').read_text())
            sdk = Path(td) / 'sdk'; shutil.copytree(ROOT / 'sdk', sdk)
            guard = sdk / 'link_guard.py'; text = guard.read_text()
            old = "pack(elf.endian + '4I'"
            assert text.count(old) == 1
            guard.write_text(text.replace(old, "pack('<4I'"))
            cmd = [sys.executable, '-B', guard, '--target', 'ppc-powermac',
                   BIN / 'powerpc-elf-ld', '-T', Path(td) / 'app.ld', '-o', Path(td) / 'mutant.elf',
                   *[Path(td) / f'{i}.o' for i in range(3)]]
            result = subprocess.run([str(x) for x in cmd], capture_output=True, text=True)
            assert result.returncode != 0 and 'incompatible per-unit generations' in result.stderr, result.stderr
    print('PPC app link: OK')


if __name__ == '__main__':
    main()
