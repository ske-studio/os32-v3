"""M2 synthetic KMOD acceptance/rejection through the real facade and loader."""
from kernel_includes import include_flags
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib

import host32
import mutpar

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_hwdev_contract as gen
import mkmod

FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__']
INCLUDES = (['-I' + str(ROOT / p) for p in ('kdebug',)] + include_flags(ROOT))


def vk32():
    image = bytearray(struct.pack('<11I', 0x32334b56, 44, 2, 1,
                                 0x100000, 1, 44, 1, 0, 45, 0) + b'X')
    crc = zlib.crc32(image)
    struct.pack_into('<I', image, 40, crc)
    return bytes(image), crc


MUTATIONS = [
    ('core-image-crc', 'm->identity.core_image_crc != core->image_crc ||', ''),
    ('contract-crc', 'm->identity.contract_crc != core_api->contract_crc ||', ''),
    ('callback-range', '!contains(r->text, r->text_size, (u32)(uptr)ops->call, 1)', '0'),
]


def fixtures(folder):
    cross = Path(os.environ['CROSS_DIR']) / 'bin'
    kernel, pair = vk32()
    subprocess.run([str(cross / 'i386-elf-gcc'), *FLAGS, *INCLUDES,
                    '-DFIXTURE_PAIR=' + str(pair) + 'U', '-c',
                    str(ROOT / 'tools/tests/hwdev_fixture.c'), '-o', str(folder / 'fixture.o')],
                   check=True)
    subprocess.run([str(cross / 'i386-elf-ld'), '-m', 'elf_i386', '--emit-relocs',
                    '-e', 'hwdev_init', '-T', str(ROOT / 'build/module.ld'),
                    str(folder / 'fixture.o'), '-o', str(folder / 'fixture.elf')], check=True)
    header = (ROOT / 'hwdev/generated/hwdev_contract.h').read_text()
    contract = int(re.search(r'#define HWDEV_CONTRACT_CRC (0x\w+)U', header)[1], 16)
    meta = dict(module_abi_gen=contract,
                imports=[dict(name=e['symbol'], kind=1, type_ver=1, index=e['index'])
                         for e in gen.slots()])
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    image, _ = mkmod.build((folder / 'fixture.elf').read_bytes(), meta, kernel, spec['version'],
                           spec['generations']['memory_layout'], module_id=11)
    elf = mkmod.Elf((folder / 'fixture.elf').read_bytes())
    h = struct.unpack_from('<36I', image)
    manifest = h[11] + elf.symbol('fixture_manifest')['value']
    callbacks = h[11] + elf.symbol('fixture_callbacks')['value']
    cases = [('valid', 0, 0, image), ('missing', 1, 2, image), ('off-silent', 2, 0, image),
             ('stale-dependency-load', 3, 14, image), ('stale-dependency-start', 4, 14, image),
             ('core-contract', 5, 6, image), ('core-layout', 6, 6, image), ('core-size', 7, 6, image),
             ('core-missing-function', 8, 12, image), ('kernel-pair', 9, 4, image),
             ('core-missing-getter', 10, 6, image), ('core-ro-span', 11, 6, image),
             ('core-version', 12, 6, image), ('core-callback-range', 13, 12, image)]

    def change(label, offset, value, rc=10, seal=True):
        b = bytearray(image)
        struct.pack_into('<I', b, offset, value)
        if seal:
            struct.pack_into('<I', b, 128, 0)
            struct.pack_into('<I', b, 128, zlib.crc32(b))
        cases.append((label, 0, rc, b))
    change('body-over-limit', 10 * 4, mkmod.POLICIES[11]['mem_max'] + 4096, 3)
    change('image-crc', manifest + 12, 2, 9, False)
    change('ops-version', 33 * 4, 2, 6)
    change('format-version', 4, 2, 3)
    change('module-abi', 6 * 4, contract ^ 1, 6)
    change('undefined-id-12', 7 * 4, 12, 3)
    for offset in (0, 4, 8, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64):
        old = struct.unpack_from('<I', image, manifest + offset)[0]
        change('manifest-' + str(offset), manifest + offset, old ^ 1)
    for offset in (0, 4):
        change('callback-abi-' + str(offset), callbacks + offset, 0)
    for offset in (8, 12):
        change('callback-text-end-' + str(offset), callbacks + offset, h[13] + h[14])
        change('callback-data-' + str(offset), callbacks + offset, h[17])
    # Remove all exports, preserving valid prefix layout/CRC; facade rejects missing initialize.
    b = bytearray(image)
    struct.pack_into('<I', b, 26 * 4, 0)
    struct.pack_into('<I', b, 28 * 4, h[28] - len(b'initialize\0'))
    struct.pack_into('<I', b, 128, 0)
    struct.pack_into('<I', b, 128, zlib.crc32(b))
    cases.append(('missing-initialize', 0, 10, b))
    for symbol, span in (('fixture_manifest', 68), ('fixture_callbacks', 16)):
        value = elf.symbol(symbol)['value']
        relocs = [struct.unpack_from('<I', image, h[21] + 4 * i)[0] for i in range(h[22])]
        refs = [off for off in relocs if struct.unpack_from('<I', image, h[11] + off)[0] == value]
        assert len(refs) == 1, (symbol, refs)
        for label, dest in (('ro-end', h[15] + h[16]), ('ro-partial', h[15] + h[16] - span + 1),
                            ('data', h[17])):
            change(symbol + '-' + label, h[11] + refs[0], dest)

    for slot in (8, 12):
        # An absent callback has no relocation and remains a genuine null pointer.
        b = bytearray(image)
        relocs = [struct.unpack_from('<I', image, h[21] + 4 * i)[0] for i in range(h[22])]
        relocs.remove(callbacks - h[11] + slot)
        struct.pack_into('<I', b, 22 * 4, len(relocs))
        struct.pack_into('<' + 'I' * len(relocs), b, h[21], *relocs)
        struct.pack_into('<I', b, callbacks + slot, 0)
        struct.pack_into('<I', b, 128, 0)
        struct.pack_into('<I', b, 128, zlib.crc32(b))
        cases.append(('callback-null-' + str(slot), 0, 10, b))
    return cases


def build(exe):
    # These addresses are installed only; no stub substitutes facade/validator code.
    aliases = [e['symbol'] for e in [e for e in gen.slots() if e['group'] == 'kernel']
               if e['symbol'] not in ('kprintf', 'kmemset', 'pgalloc_alloc_phys', 'pgalloc_free_n_owner',
                                      'gf_production_selected')]
    return ['gcc', *FLAGS, *INCLUDES, '-nostdlib', '-static', '-no-pie',
            str(ROOT / 'tools/tests/hwdev_module_host.c'), str(ROOT / 'kernel/module/module_runtime.c'),
            str(ROOT / 'kernel/module/module_image.c'),
            *['-Wl,--defsym,' + name + '=fixture_kernel' for name in aliases], '-o', str(exe)]


def run(exe, cases):
    for name, mode, rc, image in cases:
        p = host32.run([str(exe)], input=struct.pack('<3I', mode, len(image), rc) + image,
                       capture_output=True, timeout=30)
        if p.returncode:
            raise AssertionError((name, p.returncode, p.stdout, p.stderr))
        print('PASS ' + name, flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='hwdev-test-') as td:
        folder = Path(td)
        exe = folder / 'host'
        cases = fixtures(folder)
        subprocess.run(build(exe), check=True)
        run(exe, cases)
        if '--mutate' in sys.argv:
            source = (ROOT / 'kernel/module/hwdev_module.c').read_text()
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, name
                with tempfile.TemporaryDirectory(prefix='hwdev-mut-') as mt:
                    mexe = Path(mt) / 'host'
                    mutpar.build_in_tree(ROOT, mt, {'kernel/module/hwdev_module.c': source.replace(old, new)},
                                         [build(mexe)], capture_output=True)
                    try:
                        run(mexe, cases)
                    except AssertionError as exc:
                        assert exc.args[0][1] == 1 and b'FAIL ' in exc.args[0][2], exc
                        print('MUTATE ' + name + ' RED ' + str(exc), flush=True)
                    else:
                        raise AssertionError('survived ' + name)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
