"""L01: fixed ABI/slots, shared DMA boundaries and regeneration dependencies.

The fixture was captured from 466010d7, not from the code under test.
--prepare only compiles copied mutation units; it never executes a mutant.
"""
CHECK = dict(name='dep-dir-a-l01-host', tier=1,
    covers=['drivers/pci/pci.h', 'drivers/char/serial.h', 'hwdev/hwdev_imports.def', 'include/bootinfo.h', 'include/gf_production.h', 'include/kprintf.h', 'kdebug/kpeek.h', 'kdebug/recover.h', 'kernel/module/kdebug_imports.def', 'mm/pgalloc.h', 'lib/kstring.h', 'tools/gen_kdebug_imports.py', 'tools/tests/hwdev_contract_fixture.py', 'tools/tests/dep_dir_a_l01_host.c', 'tools/tests/fixtures/dep_dir_a/l01.json', 'tools/tests/test_dma8237.py', 'tools/tests/test_dma_pool.py', 'tools/tests/dma8237_host.c', 'tools/tests/dma_pool_host.c', 'tools/tests/dma_boot_host.c', 'include/dma_contract.h', 'include/pcm_contract.h', 'include/fm_contract.h', 'include/lan_contract.h', 'include/types.h', 'drivers/sound/cs4231/pcm_module.h', 'drivers/sound/fm/fm_module.h', 'drivers/net/e82557/lan_module.h', 'mm/dma_pool.h', 'mm/dma_pool_math.c', 'kernel/module/module_runtime.h', 'kernel/module/module_image.h', 'kernel/module/module_schema.h', 'kernel/irq/irq.h', 'fs/serialfs/sfs_lan.h', 'fs/serialfs/sfs_client.h', 'fs/serialfs/sfs_proto.h', 'lib/dma_contract.c', 'drivers/dma/dma8237.h', 'drivers/dma/dma8237_math.c', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/kapi.json', 'tools/prototypes/pcm_imports.def', 'tools/prototypes/fm_imports.def', 'tools/prototypes/lan_imports.def', 'hwdev/generated/hwdev_contract.h', 'tools/tests/test_module_image.py', 'tools/tests/module_image_host.c', 'tools/tests/host32.py', 'tools/mkmod.py', 'tools/module_schema.py', 'kernel/module/module_image.c', 'lib/crc32_core.inc', 'kdebug/generated/kdebug_imports.json', 'build/out/*/*_imports.h', 'build/out/*/*_imports.json', 'build/out/*.mod', 'build/out/vmkernel.lz4', 'build/out/kdebug.bin', 'drivers/dma/dma8237.c', 'drivers/block/fdc.c', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'mm/dma_pool.c', 'init/kernel.c', 'include/pci_dev_contract.h', 'include/pci_bind_contract.h', 'hwdev/generated/hwdev_imports.json', 'hwdev/generated/hwdev_layout.txt', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc', 'init/kernel_gui_shlib.inc', 'init/kernel_shell_fdd.inc'],
    needs=['make:all'], budget=60.0)

from kernel_includes import include_flags
import argparse
from hwdev_contract_fixture import assert_hwdev_contract
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
                    '-I' + str(folder), *include_flags(folder),
                    str(folder / 'probe.c'), str(folder / 'dma_contract.c'),
                    '-o', str(exe)], check=True)
    return exe


def abi(folder, fixture):
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    spec.pop('includes')
    assert hashlib.sha256(json.dumps(spec, sort_keys=True).encode()).hexdigest() == fixture['kapi_sha256']
    probe = ['#include <stddef.h>']
    for name, api in fixture['apis'].items():
        directory = {'pcm': 'drivers/sound/cs4231', 'fm': 'drivers/sound/fm',
                     'lan': 'drivers/net/e82557', 'v86': 'kernel/v86'}[name]
        text = (ROOT / directory / f'{name}_module.h').read_text()
        block = re.search(r'typedef struct \{(.*?)\} ' + api['type'] + ';', text, re.S).group(1)
        assert ' '.join(re.sub(r'/\*.*?\*/', '', block, flags=re.S).split()) == api['body']
        probe += [f'#include "{name}_module.h"',
                  f'_Static_assert(sizeof({api["type"]}) == {api["size"]}, "size");',
                  f'_Static_assert({name.upper()}_MODULE_OPS_VERSION == 1, "version");']
        for slot, member in enumerate(api['members']):
            probe += [f'_Static_assert(offsetof({api["type"]}, {member}) == {slot * 4}, "slot");']
    (folder / 'abi.c').write_text('\n'.join(probe))
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-Wall', '-Wextra', '-Werror',
                    '-D__KERNEL_BUILD__', '-fsyntax-only',
                    *include_flags(ROOT),
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
    # Private GF values follow the generated pair; public ABI snapshots stay fixed.
    assert_hwdev_contract(ROOT)
    print('PASS fixed KAPI spec, ILP32 sizes/offsets/slots, OPS_VERSION, current hwdev pair, make dependencies')



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


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_a_l01.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
