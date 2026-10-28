"""L09 NP2/host boundaries; copied ILP32 controls and one owner-exit mutant."""
import argparse
import json
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_shm_user import function
from test_dep_dir_a_l05a import digest, public_spec

ROOT = Path(__file__).resolve().parents[2]
COPIES = (
    'include/types.h', 'include/platform_device.h', 'include/platform_host.h',
    'include/net_eth.h', 'drivers/np2sysp.h', 'net/link.h',
    'include/memmap.h', 'include/time_units.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_generations.h',
    'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'platform/pc98/device_host.c', 'tools/tests/dep_dir_a_l09_host.c',
)
OLD = '    link_host_owner_exit(owner);'
NEW = '    (void)owner;'


def host_function(text, name):
    match = re.search(r'^(?:static )?(?:i32|int|void) ' + name + r'\([^)]*\)\n\{', text, re.M)
    assert match, name
    pos, depth = match.end(), 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[match.start():pos] + '\n'



def contracts():
    fixture = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l09.json').read_text())
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
    text = (ROOT / 'kapi/kapi_host.c').read_text()
    for name, expected in fixture['host'].items():
        body = host_function(text, name)
        assert body.replace('platform_host_', 'link_host_') == expected, name
        wrappers += body + '\n'
    assert 'np2sysp.h' not in spec['includes']
    assert '#include "platform_host.h"' in text
    reclaim = function((ROOT / 'exec/exec.c').read_text(), 'exec_notify_owned')
    assert reclaim.index('launch_owner_exit(id);') < reclaim.index('host_owner_exit(id);') < reclaim.index('con_sink_owner_exit(id);')
    # GF4c/2b2 adds the private fixed AR/DAC callback/type; public ABI stays fixed.
    hwdev = (ROOT / 'hwdev/generated/hwdev_contract.h').read_text()
    assert '#define HWDEV_CONTRACT_CRC 0x0dbe9e7dU' in hwdev
    assert '#define HWDEV_LAYOUT_CRC 0x772927aaU' in hwdev
    print('PASS baseline public ABI/generated files, NP2 guards, host validation/copy order, owner cleanup position, hwdev CRC')
    return wrappers


def build(folder, body, wrappers):
    for rel in COPIES:
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dest)
    (folder / 'platform/pc98/device_host.c').write_text(body)
    (folder / 'wrappers.inc').write_text(wrappers)
    exe = folder / 'host-test'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-fno-stack-protector',
                    *['-I' + str(folder / p) for p in ('.', 'include', 'drivers', 'net', 'sdk/include/os32')],
                    str(folder / 'platform/pc98/device_host.c'),
                    str(folder / 'tools/tests/dep_dir_a_l09_host.c'), '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    wrappers = contracts()
    body = (ROOT / 'platform/pc98/device_host.c').read_text()
    assert body.count(OLD) == 1
    with tempfile.TemporaryDirectory(prefix='dep-dir-a-l09-') as directory:
        folder = Path(directory)
        subprocess.run(['qemu-i386', str(build(folder / 'control', body, wrappers))], check=True)
        print('PASS NP2 absent/present, five information forwards, six owner-aware transport calls, errors and borrowed stage lifetime')
        if args.prepare:
            build(folder / 'prepared', body.replace(OLD, NEW), wrappers)
            print('PASS copied headers only; unique anchor; mutation builds -Werror (not run)')
        if args.mutate:
            result = subprocess.run(['qemu-i386', str(build(folder / 'mutant', body.replace(OLD, NEW), wrappers))])
            assert result.returncode == 1
            print('RED runtime: omitted owner cleanup (build succeeded)')


if __name__ == '__main__':
    main()
