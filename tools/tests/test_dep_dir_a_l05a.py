"""L05a fixed IDE ABI and seven generated-wrapper/platform/driver forwards.

--prepare compiles a copied mutation with -Werror; it never runs that mutation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_shm_user import function

ROOT = Path(__file__).resolve().parents[2]
COPIES = ('include/types.h', 'include/config.h', 'include/ide_contract.h',
          'include/platform_device.h', 'drivers/ide.h', 'platform/pc98/device_ide.c',
          'tools/tests/dep_dir_a_l05a_host.c')
OLD = 'return ide_read_sector(drv, lba, buf);'
NEW = 'return ide_read_sector(drv, lba ^ 1u, buf);'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def public_spec(spec):
    return {k: ([{a: b for a, b in entry.items() if a not in ('target', 'body')}
                 for entry in v] if k == 'api' else v)
            for k, v in spec.items() if k != 'includes'}


def contracts():
    fixture = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l05a.json').read_text())
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    assert digest(json.dumps(public_spec(spec), sort_keys=True).encode()) == fixture['public_sha256']
    generated = (ROOT / 'kapi/kapi_generated.c').read_text()
    assert 'ide.h' not in spec['includes'] and 'ide_contract.h' in spec['includes']
    assert '#include "ide_contract.h"' in generated and '#include "ide.h"' not in generated
    wrappers = ''
    for name, expected in fixture['wrappers'].items():
        entry = next(e for e in spec['api'] if e['name'] == name)
        target = 'platform_' + name
        body = function(generated, 'wrap_' + name)
        assert target + '(' in body
        assert body.replace(target + '(', name + '(') == expected, name
        assert (entry.get('target') == target if 'body' not in entry
                else 'return ' + target + '(' in entry['body'])
        wrappers += body + '\n'
    for path, expected in fixture['generated'].items():
        assert digest((ROOT / path).read_bytes()) == expected, path
    # GF4c/2b2 adds the private fixed AR/DAC callback/type; public ABI stays fixed.
    hwdev = (ROOT / 'hwdev/generated/hwdev_contract.h').read_text()
    for crc in ('#define HWDEV_CONTRACT_CRC 0x0dbe9e7dU',
                '#define HWDEV_LAYOUT_CRC 0x772927aaU'):
        assert crc in hwdev
    print('PASS fixed b400acb6 public API/version/slots/signatures/out/in/generated ABI; hwdev CRC')
    return fixture, wrappers


def build(folder, body, fixture, wrappers):
    for rel in COPIES:
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dest)
    (folder / 'platform/pc98/device_ide.c').write_text(body)
    (folder / 'wrappers.inc').write_text(wrappers)
    # Only copied include roots: mutation builds cannot fall back to the worktree.
    probe = ['#include <stddef.h>', '#include "ide_contract.h"',
             '_Static_assert(sizeof(IdeInfo) == 96, "IdeInfo size");']
    for field, offset in fixture['offsets'].items():
        probe.append(f'_Static_assert(offsetof(IdeInfo, {field}) == {offset}, "{field}");')
    for name, value in fixture['constants'].items():
        probe.append(f'_Static_assert({name} == {value}, "{name}");')
    (folder / 'abi.c').write_text('\n'.join(probe))
    exe = folder / 'ide'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-fno-stack-protector',
                    *['-I' + str(folder / p) for p in ('.', 'include', 'drivers')],
                    str(folder / 'abi.c'), str(folder / 'platform/pc98/device_ide.c'),
                    str(folder / 'tools/tests/dep_dir_a_l05a_host.c'), '-o', str(exe)],
                   check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    fixture, wrappers = contracts()
    body = (ROOT / 'platform/pc98/device_ide.c').read_text()
    assert body.count(OLD) == 1
    with tempfile.TemporaryDirectory(prefix='dep-dir-a-l05a-') as directory:
        folder = Path(directory)
        exe = build(folder / 'control', body, fixture, wrappers)
        for case in range(6):
            subprocess.run(['qemu-i386', str(exe), str(case)], check=True)
        print('PASS 96B/offsets/constants; seven forwards, NULL, errors, out guards, authorization, zero count')
        if args.prepare:
            build(folder / 'prepared', body.replace(OLD, NEW), fixture, wrappers)
            print('PASS copied include closure; unique anchor; substitution builds -Werror (not run)')
        if args.mutate:
            exe = build(folder / 'mutant', body.replace(OLD, NEW), fixture, wrappers)
            result = subprocess.run(['qemu-i386', str(exe), '0'])
            assert result.returncode == 1
            print('RED runtime: wrong read_sector LBA (build succeeded)')


if __name__ == '__main__':
    main()
