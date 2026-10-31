"""L09 NP2/host boundaries; copied ILP32 controls and one owner-exit mutant."""
CHECK = dict(name='dep-dir-a-l09-host', tier=1,
    covers=['drivers/pci/pci.h', 'drivers/char/serial.h', 'hwdev/hwdev_imports.def', 'include/bootinfo.h', 'include/gf_production.h', 'include/kprintf.h', 'kdebug/kpeek.h', 'kdebug/recover.h', 'kernel/module/kdebug_imports.def', 'mm/pgalloc.h', 'lib/kstring.h', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/module_schema.py', 'tools/tests/hwdev_contract_fixture.py', 'include/memmap.h', 'include/platform_device.h', 'include/time_units.h', 'include/types.h', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h', 'sdk/kapi.json', 'kapi/kapi_generated.c', 'tools/tests/test_dep_dir_a_l05a.py', 'tools/tests/fixtures/dep_dir_a/l05a.json', 'tools/tests/test_shm_user.py', 'tools/tests/kernel_layout.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'sdk/include/os32/os32_kapi_slots.h', 'sdk/rust/os32api/src/kapi_generated.rs', 'hwdev/generated/hwdev_contract.h', 'exec/exec.c', 'drivers/video/pc98/backend_pegc.c', 'gfx/gfx_core.c', 'init/kernel.c', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shm.c', 'kernel/v86/v86.c', 'kernel/v86/v86_mem.c', 'kernel/v86/v86_snapshot.c', 'tools/tests/shm_user_host.c', 'platform/pc98/device_ide.c', 'tools/tests/dep_dir_a_l05a_host.c', 'include/platform_host.h', 'include/net_eth.h', 'drivers/char/np2sysp.h', 'net/link.h', 'platform/pc98/device_host.c', 'tools/tests/dep_dir_a_l09_host.c', 'kapi/kapi_host.c', 'kapi/range_internal.h', 'tools/tests/fixtures/dep_dir_a/l09.json', 'hwdev/generated/hwdev_imports.json', 'hwdev/generated/hwdev_layout.txt', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py'],
    needs=['make:all'], budget=60.0)

from kernel_includes import include_flags
import argparse
from hwdev_contract_fixture import assert_hwdev_contract
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
    'include/net_eth.h', 'drivers/char/np2sysp.h', 'net/link.h',
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
        if name == 'host_range_ok':
            body = host_function((ROOT / 'kapi/range_internal.h').read_text(), 'kapi_user_range_ok')
            body = body.replace('kapi_user_range_ok', name)
        else:
            body = host_function(text, name).replace('kapi_user_range_ok', 'host_range_ok')
        assert body.replace('platform_host_', 'link_host_') == expected, name
        wrappers += body + '\n'
    assert 'np2sysp.h' not in spec['includes']
    assert '#include "platform_host.h"' in text
    reclaim = function((ROOT / 'exec/exec.c').read_text(), 'exec_notify_owned')
    assert reclaim.index('launch_owner_exit(id);') < reclaim.index('host_owner_exit(id);') < reclaim.index('con_sink_owner_exit(id);')
    # Private GF values follow the generated pair; public ABI snapshots stay fixed.
    assert_hwdev_contract(ROOT)
    print('PASS baseline public ABI/generated files, NP2 guards, host validation/copy order, owner cleanup position, current hwdev pair')
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
                    *include_flags(folder),
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


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_a_l09.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
