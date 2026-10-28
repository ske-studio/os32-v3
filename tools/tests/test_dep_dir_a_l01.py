"""L01: fixed ABI/slots, shared DMA boundaries and regeneration dependencies.

The fixture was captured from 466010d7, not from the code under test.
--prepare only compiles copied mutation units; it never executes a mutant.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tools/tests/fixtures/dep_dir_a/l01.json'
OLD = 'if (dma_crosses_64k(pa, size)) return 0;'
NEW = 'if (dma_crosses_64k(pa, size) && 0) return 0;'


def build_dma(folder, body):
    folder.mkdir(parents=True, exist_ok=True)
    # Self-contained copied contract: no fallback to the repository include path.
    shutil.copy(ROOT / 'include/dma_contract.h', folder)
    shutil.copy(ROOT / 'include/types.h', folder)
    shutil.copy(ROOT / 'tools/tests/dep_dir_a_l01_host.c', folder / 'probe.c')
    (folder / 'dma_contract.c').write_text(body)
    exe = folder / 'dma'
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-m32', '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-I' + str(folder), '-I' + str(ROOT / 'sdk/include/os32'),
                    str(folder / 'probe.c'), str(folder / 'dma_contract.c'),
                    '-o', str(exe)], check=True)
    return exe


def abi(folder, fixture):
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    spec.pop('includes')
    assert hashlib.sha256(json.dumps(spec, sort_keys=True).encode()).hexdigest() == fixture['kapi_sha256']
    probe = ['#include <stddef.h>']
    for name, api in fixture['apis'].items():
        text = (ROOT / f'kernel/{name}_module.h').read_text()
        block = re.search(r'typedef struct \{(.*?)\} ' + api['type'] + ';', text, re.S).group(1)
        assert ' '.join(re.sub(r'/\*.*?\*/', '', block, flags=re.S).split()) == api['body']
        probe += [f'#include "kernel/{name}_module.h"',
                  f'_Static_assert(sizeof({api["type"]}) == {api["size"]}, "size");',
                  f'_Static_assert({name.upper()}_MODULE_OPS_VERSION == 1, "version");']
        for slot, member in enumerate(api['members']):
            probe += [f'_Static_assert(offsetof({api["type"]}, {member}) == {slot * 4}, "slot");']
    (folder / 'abi.c').write_text('\n'.join(probe))
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-Wall', '-Wextra', '-Werror',
                    '-D__KERNEL_BUILD__', '-fsyntax-only',
                    *['-I' + str(ROOT / p) for p in
                      ('.', 'include', 'kernel', 'drivers', 'fs', 'sdk/include/os32')],
                    str(folder / 'abi.c')], check=True)
    for name in ('pcm', 'fm', 'lan'):
        headers = [f'include/{name}_contract.h'] + (['include/dma_contract.h'] if name == 'pcm' else [])
        definition = json.loads((ROOT / f'tools/prototypes/{name}_imports.def').read_text())
        for header in headers:
            assert header in definition['contracts']
            # -W simulates only the contract being newer, without touching files.
            result = subprocess.run(['make', '-n', '-j1', '-W', header,
                                     f'build/out/{name}/{name}_imports.h'], cwd=ROOT,
                                    capture_output=True, text=True, check=True)
            assert f'gen_sqlite_imports.py --module {name} ' in result.stdout, result.stdout
    # GF4c/2a explicitly changes the private C plan/capture layout; public ABI stays fixed.
    hwdev = (ROOT / 'hwdev/generated/hwdev_contract.h').read_text()
    assert '#define HWDEV_CONTRACT_CRC 0x7d90586cU' in hwdev
    assert '#define HWDEV_LAYOUT_CRC 0xe0c871d7U' in hwdev
    print('PASS fixed KAPI spec, ILP32 sizes/offsets/slots, OPS_VERSION, hwdev CRC, make dependencies')



def generations(folder, fixture):
    import sys
    sys.path.insert(0, str(ROOT / 'tools'))
    import mkmod
    from test_module_image import build_validator, validate, fnv, crc, word
    exe = build_validator(folder)
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    pair = mkmod.pair_crc((ROOT / 'build/out/vmkernel.lz4').read_bytes())
    for name, module_id in (('pcm', 7), ('fm', 6), ('lan', 9), ('kdebug', 1)):
        metadata_path = ('kdebug/generated/kdebug_imports.json' if name == 'kdebug'
                         else f'build/out/{name}/{name}_imports.json')
        metadata = json.loads((ROOT / metadata_path).read_text())
        rows = [{k: v for k, v in e.items() if k != 'source'} for e in metadata['imports']]
        assert hashlib.sha256(json.dumps(rows, sort_keys=True).encode()).hexdigest() == fixture['imports_sha256'][name]
        old = fixture['old_generations'][name]
        current = metadata['module_abi_gen']
        assert old != current
        if name == 'kdebug':
            exports = [(i, fnv(mkmod.OPS_NAMES[i - 1]), 1)
                       for i in list(range(1, 17)) + [18, 19, 20]]
            image = (ROOT / 'build/out/kdebug.bin').read_bytes()
        else:
            exports = [(1, fnv('initialize'), 1)]
            image = (ROOT / f'build/out/{name}.mod').read_bytes()
        contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                        abi=current, pair=pair, ops=4 if name == 'kdebug' else 1,
                        module_id=module_id, runner='qemu', exports=exports,
                        imports=[(fnv(e['name']), e['type_ver'], e['kind']) for e in metadata['imports']])
        result = validate(exe, image, **contract)
        assert result.returncode == 0 and result.stdout == b'GREEN\n', result
        stale = bytearray(image)
        word(stale, 6, old)
        result = validate(exe, crc(stale), **contract)
        assert result.returncode == 1 and result.stdout == b'RED 06\n', result
        print(f'PASS {name}: 0x{old:08x} -> 0x{current:08x}; paired image accepted, old ABI rejected')


def prepare(folder, body):
    assert body.count(OLD) == 1
    build_dma(folder / 'direct', body.replace(OLD, NEW))
    # Existing mutation harnesses must still see the extracted implementation
    # and headers, and all replacement anchors must match exactly once.
    import test_dma8237 as d8237
    import test_dma_pool as pool
    for suite in (d8237, pool):
        for index, (pattern, replacement, label, *where) in enumerate(suite.MUTATIONS):
            target = where[0] if where else suite.SRC
            original = target.read_text()
            assert len(re.findall(pattern, original)) == 1, label
            substituted = re.sub(pattern, replacement, original)
            dest = folder / (suite.__name__ + str(index))
            dest.mkdir()
            if target == suite.SRC:
                suite.host_build(dest, substituted)
            else:
                suite.host_build(dest, None, substituted)
    print('PASS preparation only: copied headers, unique anchors, all DMA substitutions compile -Werror')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    fixture = json.loads(FIXTURE.read_text())
    body = (ROOT / 'lib/dma_contract.c').read_text()
    assert body.count(OLD) == 1
    with tempfile.TemporaryDirectory(prefix='dep-dir-a-l01-') as directory:
        folder = Path(directory)
        exe = build_dma(folder / 'control', body)
        subprocess.run(['qemu-i386', str(exe)], check=True)
        abi(folder, fixture)
        generations(folder, fixture)
        print('PASS shared DMA: zero, 64KiB boundary, 16MiB end, u32 overflow')
        if args.prepare:
            prepare(folder, body)
        if args.mutate:
            exe = build_dma(folder / 'mutant', body.replace(OLD, NEW))
            result = subprocess.run(['qemu-i386', str(exe)], capture_output=True)
            assert result.returncode == 1, result
            print('RED runtime: shared DMA crossing rejection disabled (build succeeded)')


if __name__ == '__main__':
    main()
