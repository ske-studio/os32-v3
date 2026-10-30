"""L04 forwarding and fixed pre-change V86 selection/lifetime and public ABI.

--prepare builds a substituted copy with -Werror without executing a mutant.
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
COPIES = ('include/types.h', 'include/platform_device.h', 'drivers/block/loop_dev.h',
          'platform/pc98/device_loop.c', 'tools/tests/dep_dir_a_l04_host.c')
OLD = 'return loop_dev_read_lba(slot, lba, count, buf);'
NEW = 'return loop_dev_read_lba(slot ^ 1, lba, count, buf);'


def build(folder, body):
    for rel in COPIES:
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dest)
    (folder / 'platform/pc98/device_loop.c').write_text(body)
    exe = folder / 'loop'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-fno-stack-protector',
                    *['-I' + str(folder / p) for p in ('include', 'drivers')],
                    str(folder / 'platform/pc98/device_loop.c'),
                    str(folder / 'tools/tests/dep_dir_a_l04_host.c'), '-o', str(exe)],
                   check=True)
    return exe


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fixed_contracts():
    fixture = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l04.json').read_text())
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    generated = (ROOT / 'kapi/kapi_generated.c').read_text()
    assert '"platform_device.h"' in generated
    for entry in spec['api']:
        if entry['name'] in ('loop_attach', 'loop_detach', 'loop_status'):
            target = 'platform_loop_dev_' + entry['name'].removeprefix('loop_')
            assert entry.pop('target') == target
            assert target + '(' in function(generated, 'wrap_' + entry['name'])
    assert digest(json.dumps({k: v for k, v in spec.items() if k != 'includes'},
                             sort_keys=True).encode()) == fixture['api_sha256']
    source = (ROOT / 'kernel/v86/v86.c').read_text()
    assert '#include "platform_device.h"' in source and '"loop_dev.h"' not in source
    for name, expected in fixture['v86_functions'].items():
        body = function(source, name)
        assert body.count('platform_loop_dev_') == (2 if name == 'v86_disk_test' else 3)
        assert digest(body.replace('platform_loop_dev_', 'loop_dev_').encode()) == expected
    hle = (ROOT / 'kernel/v86/v86_bios.c').read_text()
    assert 'platform_device.h' not in hle and 'platform_loop_dev_' not in hle
    detach = function(hle, 'v86_bios_detach_disk')
    assert detach.count('loop_dev_detach(') == 1
    assert digest(detach.encode()) == fixture['v86_detach_sha256']
    for path, expected in fixture['generated'].items():
        assert digest((ROOT / path).read_bytes()) == expected, path
    print('PASS fixed b400acb6 API/slots/version/signatures/generated ABI; V86 CHS/LBA and lifetime')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    fixed_contracts()
    body = (ROOT / 'platform/pc98/device_loop.c').read_text()
    assert body.count(OLD) == 1
    with tempfile.TemporaryDirectory(prefix='dep-dir-a-l04-') as directory:
        folder = Path(directory)
        exe = build(folder / 'control', body)
        subprocess.run(['qemu-i386', str(exe)], check=True)
        print('PASS six real forwards: all arguments, NULL outputs, values, errors and one call each')
        if args.prepare:
            build(folder / 'prepared', body.replace(OLD, NEW))
            print('PASS copied include closure; unique anchor; substituted copy builds -Werror (not run)')
        if args.mutate:
            exe = build(folder / 'mutant', body.replace(OLD, NEW))
            result = subprocess.run(['qemu-i386', str(exe)])
            assert result.returncode == 1
            print('RED runtime: wrong LBA slot (build succeeded)')


if __name__ == '__main__':
    main()
