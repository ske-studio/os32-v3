"""L06 format/session KAPI boundary; --prepare builds but never runs a mutation."""
CHECK = dict(name='dep-dir-a-l06-host', tier=1,
    covers=['sys/platform/pc98/gfx_surfaces.c', 'sys/include/callconv.h', 'sys/platform/pc98/bios_snapshot.h', 'sys/platform/pc98/boot_capture.c', 'sys/platform/pc98/bootinfo_wire.h', 'sys/platform/pc98/bootinfo_native.h', 'sys/platform/pc98/legacy/lowmap.c', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/dev/hwdev/generated/hwdev_layout.txt', 'tools/hwdev_layout.json', 'sys/dev/pci/pci.h', 'sys/dev/char/serial.h', 'sys/dev/hwdev/hwdev_imports.def', 'sys/include/bootinfo.h', 'sys/include/gf_production.h', 'sys/include/kprintf.h', 'sys/platform/pc98/kdebug/kpeek.h', 'sys/dev/video/geforce/diag/recover.h', 'sys/kern/module/kdebug_imports.def', 'sys/kern/mm/pgalloc.h', 'sys/kern/kstring.h', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/module_schema.py', 'tools/tests/hwdev_contract_fixture.py', 'sys/fs/ext2/ext2.h', 'sys/fs/serialfs/serialfs.h', 'sys/fs/serialfs/sfs_client.h', 'sys/fs/serialfs/sfs_proto.h', 'sys/fs/vfs.h', 'sys/include/memmap.h', 'sys/include/platform_device.h', 'sys/include/time_units.h', 'sys/include/types.h', 'sys/kern/dev/device_fs.c', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_snd2.h', 'sdk/include/os32/os32_in2.h', 'sdk/include/os32/os32_v86s.h', 'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_gfx2.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h', 'tools/tests/dep_dir_a_l06_host.c', 'sdk/kapi.json', 'sys/kapi/kapi_generated.c', 'sys/fs/vfs.c', 'sys/fs/serialfs/sfs_session_fg.c', 'tools/tests/test_dep_dir_a_l05a.py', 'tools/tests/fixtures/dep_dir_a/l06.json', 'tools/tests/fixtures/dep_dir_a/l05a.json', 'tools/tests/test_shm_user.py', 'tools/tests/kernel_layout.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'sys/include/config.h', 'sys/include/ide_contract.h', 'sys/dev/ata/ide.h', 'sys/dev/ide/device_ide.c', 'sdk/include/os32/os32_kapi_slots.h', 'sdk/rust/os32api/src/kapi_generated.rs', 'sys/dev/hwdev/generated/hwdev_contract.h', 'tools/tests/dep_dir_a_l05a_host.c', 'sys/kern/exec/exec.c', 'sys/platform/pc98/video/backend_pegc.c', 'sys/hal/gfx/gfx_core.c', 'sys/platform/pc98/kernel.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shm.c', 'sys/platform/pc98/legacy/v86.c', 'sys/platform/pc98/legacy/v86_mem.c', 'sys/platform/pc98/bios_snapshot.c', 'tools/tests/shm_user_host.c', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=2.9,
    basis=['[ABI]', 'KAPI_SPEC §3-1'],
)

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from kernel_includes import include_flags
from test_shm_user import function
from test_dep_dir_a_l05a import digest, public_spec

ROOT = Path(__file__).resolve().parents[2]
COPIES = ('sys/include/callconv.h',
    'sys/fs/ext2/ext2.h', 'sys/fs/serialfs/serialfs.h', 'sys/fs/serialfs/sfs_client.h', 'sys/fs/serialfs/sfs_proto.h', 'sys/fs/vfs.h',
    'sys/include/memmap.h', 'sys/include/platform_device.h', 'sys/include/time_units.h',
    'sys/include/types.h', 'sys/kern/dev/device_fs.c',
    'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h',
    'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_snd2.h', 'sdk/include/os32/os32_in2.h', 'sdk/include/os32/os32_v86s.h', 'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_gfx2.h', 'sdk/include/os32/os32_ls.h',
    'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h',
    'tools/tests/dep_dir_a_l06_host.c',
)
OLD = 'return ext2_format_at(drv, start_lba, length);'
NEW = 'return ext2_format_at(drv, length, start_lba);'


def contracts():
    fixture = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l06.json').read_text())
    abi = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l05a.json').read_text())
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    generated = (ROOT / 'sys/kapi/kapi_generated.c').read_text()
    wrappers = ''
    for name, expected in fixture['wrappers'].items():
        entry = next(e for e in spec['api'] if e['name'] == name)
        original = {'sfs_begin': 'serialfs_session_begin',
                    'sfs_end': 'serialfs_session_end'}.get(name, name)
        target = 'platform_' + original
        body = function(generated, 'wrap_' + name)
        assert target + '(' in body
        assert body.replace(target + '(', original + '(') == expected, name
        assert (target == entry.get('target') if 'body' not in entry
                else 'return ' + target + '(' in entry['body'])
        wrappers += body + '\n'
    assert '#include "kbd.h"' not in (ROOT / 'sys/fs/vfs.c').read_text()
    session = (ROOT / 'sys/fs/serialfs/sfs_session_fg.c').read_text()
    assert '#include "lan_contract.h"' in session and '"e82557.h"' not in session
    print('PASS fixed baseline public API/generated ABI and exact wrapper guards; A3 includes')
    return wrappers


def build(folder, body, wrappers):
    for rel in COPIES:
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dest)
    (folder / 'sys/kern/dev/device_fs.c').write_text(body)
    (folder / 'wrappers.inc').write_text(wrappers)
    exe = folder / 'fs-test'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-fno-stack-protector',
                    *include_flags(folder),
                    str(folder / 'sys/kern/dev/device_fs.c'),
                    str(folder / 'tools/tests/dep_dir_a_l06_host.c'), '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    wrappers = contracts()
    body = (ROOT / 'sys/kern/dev/device_fs.c').read_text()
    assert body.count(OLD) == 1
    with tempfile.TemporaryDirectory(prefix='dep-dir-a-l06-') as directory:
        folder = Path(directory)
        subprocess.run(['qemu-i386', str(build(folder / 'control', body, wrappers))], check=True)
        print('PASS four forwards, edge arguments, errors and write authorization rejection')
        if args.prepare:
            build(folder / 'prepared', body.replace(OLD, NEW), wrappers)
            print('PASS copied headers only; unique anchor; substituted copy builds -Werror (not run)')
        if args.mutate:
            result = subprocess.run(['qemu-i386', str(build(folder / 'mutant', body.replace(OLD, NEW), wrappers))])
            assert result.returncode == 1
            print('RED runtime: swapped format_at range (build succeeded)')


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_a_l06.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
