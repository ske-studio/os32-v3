"""L09 NP2/host boundaries; copied ILP32 controls and one owner-exit mutant."""
CHECK = dict(name='dep-dir-a-l09-host', tier=1,
    covers=['sys/platform/pc98/gfx_surfaces.c', 'sys/include/callconv.h', 'sys/platform/pc98/bios_snapshot.h', 'sys/platform/pc98/boot_capture.c', 'sys/platform/pc98/bootinfo_wire.h', 'sys/platform/pc98/bootinfo_native.h', 'sys/platform/pc98/legacy/lowmap.c', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'tools/hwdev_layout.json', 'sys/dev/pci/pci.h', 'sys/dev/char/serial.h', 'sys/dev/hwdev/hwdev_imports.def', 'sys/include/bootinfo.h', 'sys/include/gf_production.h', 'sys/include/kprintf.h', 'sys/platform/pc98/kdebug/kpeek.h', 'sys/dev/video/geforce/diag/recover.h', 'sys/kern/module/kdebug_imports.def', 'sys/kern/mm/pgalloc.h', 'sys/kern/kstring.h', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/module_schema.py', 'tools/tests/hwdev_contract_fixture.py', 'sys/include/memmap.h', 'sys/include/platform_device.h', 'sys/include/time_units.h', 'sys/include/types.h', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_snd2.h', 'sdk/include/os32/os32_in2.h', 'sdk/include/os32/os32_v86s.h', 'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_gfx2.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h', 'sdk/kapi.json', 'sys/kapi/kapi_generated.c', 'tools/tests/test_dep_dir_a_l05a.py', 'tools/tests/fixtures/dep_dir_a/l05a.json', 'tools/tests/test_shm_user.py', 'tools/tests/kernel_layout.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'sdk/include/os32/os32_kapi_slots.h', 'sdk/rust/os32api/src/kapi_generated.rs', 'sys/dev/hwdev/generated/hwdev_contract.h', 'sys/kern/exec/exec.c', 'sys/platform/pc98/video/backend_pegc.c', 'sys/hal/gfx/gfx_core.c', 'sys/platform/pc98/kernel.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shm.c', 'sys/platform/pc98/legacy/v86.c', 'sys/platform/pc98/legacy/v86_mem.c', 'sys/platform/pc98/bios_snapshot.c', 'tools/tests/shm_user_host.c', 'sys/dev/ide/device_ide.c', 'tools/tests/dep_dir_a_l05a_host.c', 'sys/include/platform_host.h', 'sys/include/net_eth.h', 'sys/dev/char/np2sysp.h', 'sys/net/link.h', 'sys/platform/pc98/device_host.c', 'sys/kern/dev/device_host.c', 'tools/tests/dep_dir_a_l09_host.c', 'sys/kapi/kapi_host.c', 'sys/kapi/range_internal.h', 'tools/tests/fixtures/dep_dir_a/l09.json', 'sys/dev/hwdev/generated/hwdev_imports.json', 'sys/dev/hwdev/generated/hwdev_layout.txt', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=3.2,
    basis=['[ABI]', 'KAPI_SPEC §3-1'],
)

from exec_source import exec_source
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
COPIES = ('sys/include/callconv.h',
    'sys/include/types.h', 'sys/include/platform_device.h', 'sys/include/platform_host.h',
    'sys/include/net_eth.h', 'sys/dev/char/np2sysp.h', 'sys/net/link.h',
    'sys/include/memmap.h', 'sys/include/time_units.h',
    'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_snd2.h', 'sdk/include/os32/os32_in2.h', 'sdk/include/os32/os32_v86s.h', 'sdk/include/os32/os32_host_notice.h',
    'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_generations.h',
    'sdk/include/os32/os32_gfx2.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'sys/platform/pc98/device_host.c', 'sys/kern/dev/device_host.c', 'tools/tests/dep_dir_a_l09_host.c',
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
    generated = (ROOT / 'sys/kapi/kapi_generated.c').read_text()
    wrappers = ''
    for name, expected in fixture['wrappers'].items():
        body = function(generated, 'wrap_' + name)
        entry = next(e for e in spec['api'] if e['name'] == name)
        assert entry['target'] == 'platform_' + name
        assert body.replace('platform_' + name + '(', name + '(') == expected
        wrappers += body + '\n'
    text = (ROOT / 'sys/kapi/kapi_host.c').read_text()
    for name, expected in fixture['host'].items():
        if name == 'host_range_ok':
            body = host_function((ROOT / 'sys/kapi/range_internal.h').read_text(), 'kapi_user_range_ok')
            body = body.replace('kapi_user_range_ok', name)
        else:
            body = host_function(text, name).replace('kapi_user_range_ok', 'host_range_ok')
        assert body.replace('platform_host_', 'link_host_') == expected, name
        wrappers += body + '\n'
    assert 'np2sysp.h' not in spec['includes']
    assert '#include "platform_host.h"' in text
    reclaim = function(exec_source(ROOT), 'exec_notify_owned')
    assert reclaim.index('launch_owner_exit(id);') < reclaim.index('exec_host_owner_exit(id);') < reclaim.index('con_sink_owner_exit(id);')
    # Private GF values follow the generated pair; public ABI snapshots stay fixed.
    assert_hwdev_contract(ROOT)
    print('PASS baseline public ABI/generated files, NP2 guards, host validation/copy order, owner cleanup position, current hwdev pair')
    return wrappers


def build(folder, body, wrappers):
    for rel in COPIES:
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dest)
    (folder / 'sys/kern/dev/device_host.c').write_text(body)
    (folder / 'wrappers.inc').write_text(wrappers)
    exe = folder / 'host-test'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-fno-stack-protector',
                    *include_flags(folder),
                    str(folder / 'sys/platform/pc98/device_host.c'), str(folder / 'sys/kern/dev/device_host.c'),
                    str(folder / 'tools/tests/dep_dir_a_l09_host.c'), '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    wrappers = contracts()
    body = (ROOT / 'sys/kern/dev/device_host.c').read_text()
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
