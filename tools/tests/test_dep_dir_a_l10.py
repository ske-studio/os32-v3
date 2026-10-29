"""L10 resident KCG controls; copied ILP32 controls and one scale mutant."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_shm_user import function
from test_dep_dir_a_l05a import digest, public_spec

ROOT = Path(__file__).resolve().parents[2]
COPIES = (
    'include/types.h', 'include/platform_device.h', 'include/kcg_contract.h',
    'include/pc98.h', 'drivers/kcg.h', 'drivers/kcg.c',
    'kernel/kcg_module.h', 'kernel/module_runtime.h', 'kernel/module_image.h',
    'kernel/module_schema.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_generations.h',
    'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'platform/pc98/device_kcg.c', 'tools/tests/dep_dir_a_l10_host.c',
)
OLD = '    kcg_set_scale(scale);'
NEW = '    (void)scale;'


def contracts():
    fixture = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l10.json').read_text())
    abi = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l05a.json').read_text())
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    assert digest(json.dumps(public_spec(spec), sort_keys=True).encode()) == abi['public_sha256']
    for path, expected in abi['generated'].items():
        assert digest((ROOT / path).read_bytes()) == expected, path
    generated = (ROOT / 'kapi/kapi_generated.c').read_text()
    wrappers = ''
    for name, expected in fixture['wrappers'].items():
        body = function(generated, 'wrap_' + name)
        entry = next(e for e in spec['api'] if e['name'] == name)
        assert entry['target'] == 'platform_' + name
        assert body.replace('platform_' + name + '(', name + '(') == expected
        wrappers += body + '\n'
    assert 'kcg.h' not in spec['includes'] and 'kcg_contract.h' in spec['includes']
    assert '#include "kcg_contract.h"' in generated and '#include "kcg.h"' not in generated
    module = (ROOT / 'kernel/kcg_module.c').read_text()
    assert '#include "kcg_contract.h"' in module and '#include "kcg.h"' not in module
    assert 'void kcg_rom_read_ank(u8 ch, u8 *buf) { public_api()->read_ank(ch, buf); }' in module
    assert 'void kcg_rom_read_kanji(u16 jis, u8 *buf) { public_api()->read_kanji(jis, buf); }' in module
    # GF4c adds the private cell callback, route step and verify-record flag; public ABI stays fixed.
    hwdev = (ROOT / 'hwdev/generated/hwdev_contract.h').read_text()
    assert '#define HWDEV_CONTRACT_CRC 0x50a115a8U' in hwdev
    assert '#define HWDEV_LAYOUT_CRC 0x1eae31dcU' in hwdev
    print('PASS baseline public ABI/generated files, KCG controls and ROM boundary, hwdev CRC')
    return wrappers


def build(folder, body, wrappers):
    for rel in COPIES:
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dest)
    (folder / 'platform/pc98/device_kcg.c').write_text(body)
    (folder / 'wrappers.inc').write_text(wrappers)
    exe = folder / 'host-test'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-fno-stack-protector',
                    *['-I' + str(folder / p) for p in ('.', 'include', 'drivers', 'kernel', 'sdk/include/os32')],
                    str(folder / 'platform/pc98/device_kcg.c'),
                    str(folder / 'drivers/kcg.c'),
                    str(folder / 'tools/tests/dep_dir_a_l10_host.c'), '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    wrappers = contracts()
    body = (ROOT / 'platform/pc98/device_kcg.c').read_text()
    assert body.count(OLD) == 1
    with tempfile.TemporaryDirectory(prefix='dep-dir-a-l10-') as directory:
        folder = Path(directory)
        subprocess.run(['qemu-i386', str(build(folder / 'control', body, wrappers))], check=True)
        print('PASS resident init/reset, extreme scale clamp, NULL/path NOSYS, no ROM operation in controls; ROM read stubs')
        if args.prepare:
            build(folder / 'prepared', body.replace(OLD, NEW), wrappers)
            print('PASS copied headers only; unique anchor; mutation builds -Werror (not run)')
        if args.mutate:
            result = subprocess.run(['qemu-i386', str(build(folder / 'mutant', body.replace(OLD, NEW), wrappers))])
            assert result.returncode == 1
            print('RED runtime: omitted set_scale forwarding (build succeeded)')


if __name__ == '__main__':
    main()
