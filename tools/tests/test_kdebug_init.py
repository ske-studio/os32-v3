"""S1a: actual init has no imports; actual loader rejects and recovers.
記録: tools/tests/kdebug_init_tdd.md
"""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

import host32

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_kdebug_imports as gen
import mkmod
from test_mkmod import vk32

FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__']
INCLUDES = ['-I' + str(ROOT / d) for d in
            ('.', 'include', 'kernel', 'lib', 'fs', 'arch/x86', 'platform/pc98',
             'sdk/include', 'sdk/include/os32')]


def command(args):
    subprocess.run([str(a) for a in args], check=True)


def seal(image):
    struct.pack_into('<I', image, 128, 0)
    struct.pack_into('<I', image, 128, zlib.crc32(image))
    return image


def main():
    with tempfile.TemporaryDirectory(prefix='kdebug-init-') as td:
        folder = Path(td)
        meta = gen.generate(gen.load_def(), folder, 'S1')
        for generated in (ROOT / 'kdebug/generated').iterdir():
            if generated.suffix in ('.c', '.h', '.S', '.json'):
                assert generated.read_bytes() == (folder / generated.name).read_bytes(), generated
        cross = Path(os.environ['CROSS_DIR']) / 'bin'
        inc = [*INCLUDES, '-I' + td]
        host = ['gcc', *FLAGS, *inc, '-nostdlib', '-static', '-no-pie']
        exe = folder / 'init'
        harness = ROOT / 'tools/tests/kdebug_init_host.c'
        source = ROOT / 'kdebug/kdebug_main.c'
        command([*host, harness, source, '-o', exe])
        for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
            assert host32.run([str(exe)], runner=runner).returncode == 0
        # A called import and a missing export must each fail by assertion.
        if '--mutate' in sys.argv:
            for label, old, new in (
                ('init-import', '    *candidate = kdebug_ops;',
                 '    kprintf(7, "forbidden");\n    *candidate = kdebug_ops;'),
                ('init-empty', '    *candidate = kdebug_ops;', '    (void)candidate;')):
                mutant = folder / (label + '.c')
                mutant.write_text('#include "kprintf.h"\n' + source.read_text().replace(old, new))
                command([*host, harness, mutant, '-o', exe])
                assert host32.run([str(exe)]).returncode == 1, label
                print('MUTATE', label, 'RED', flush=True)
        # Build the real module independently of pre-existing product artifacts.
        objects = []
        for src in (ROOT / 'kdebug/crt_diag.c', source,
                    folder / 'kdebug_import_slots.c', folder / 'kdebug_import_stubs.S'):
            obj = folder / (src.stem + '.o')
            command([cross / 'i386-elf-gcc', *FLAGS, *inc, '-c', src, '-o', obj])
            objects.append(obj)
        elf = folder / 'module.elf'
        command([cross / 'i386-elf-ld', '-m', 'elf_i386', '--emit-relocs',
                 '-T', ROOT / 'build/module.ld', *objects, '-o', elf])
        kernel, pair = vk32()
        kapi = json.loads((ROOT / 'sdk/kapi.json').read_text())['version']
        image, stats = mkmod.build(elf.read_bytes(), meta, kernel, kapi_abi_gen=kapi)
        exe = folder / 'loader'
        command([*host, ROOT / 'tools/tests/kdebug_loader_host.c',
                 ROOT / 'kernel/module.c', ROOT / 'kernel/module_image.c',
                 folder / 'kdebug_kexports.c', '-o', exe])
        cases = [('off', 0, 0, 0, image), ('other-config', 2, 0, 0, image),
                 ('running', 1, 0, 1, image)]
        for fail, state in ((1, 2), (2, 8), (3, 8), (4, 7), (5, 7), (6, 8), (7, 5)):
            cases.append((f'failure-{fail}', 1, fail, state, image))
        for label, word, value, state in (
            ('header', 0, 0, 3), ('old-pair', 31, pair ^ 1, 4),
            ('abi', 6, meta['module_abi_gen'] ^ 1, 6), ('crc', 32, 0, 9)):
            bad = bytearray(image)
            struct.pack_into('<I', bad, word * 4, value)
            if word != 32:
                seal(bad)
            cases.append((label, 1, 0, state, bad))
        words = struct.unpack_from('<36I', image)
        for label, off, value, state in (
            ('reloc-range', words[21], words[12], 11),
            ('import-hash', words[23], 0, 12),
            ('export-slot', words[25] + 4, 0x10000, 3)):
            bad = bytearray(image)
            struct.pack_into('<I', bad, off, value)
            cases.append((label, 1, 0, state, seal(bad)))
        # The actual loader calls this deliberately failing, side-effect-free init.
        bad = bytearray(image)
        init = words[11] + words[29]
        bad[init:init + 6] = b'\xb8\x01\x00\x00\x00\xc3'
        cases.append(('init-failed', 1, 0, 10, seal(bad)))
        for label, config, fail, state, data in cases:
            payload = struct.pack('<5I', config, fail, state, len(data), pair) + data
            for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
                result = host32.run([str(exe)], input=payload, runner=runner)
                assert result.returncode == 0, (label, result.returncode)
            print('PASS loader', label, flush=True)
        print('PASS init: candidate only, imports=0; loader cases=' + str(len(cases)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
