"""L06 format/session KAPI boundary; --prepare builds but never runs a mutation."""
CHECK = dict(name='dep-dir-a-l06-host', tier=1,
    covers=['drivers/pci/pci.h', 'drivers/char/serial.h', 'hwdev/hwdev_imports.def', 'include/bootinfo.h', 'include/gf_production.h', 'include/kprintf.h', 'kdebug/kpeek.h', 'kdebug/recover.h', 'kernel/module/kdebug_imports.def', 'mm/pgalloc.h', 'lib/kstring.h', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/module_schema.py', 'tools/tests/hwdev_contract_fixture.py', 'fs/ext2/ext2.h', 'fs/serialfs/serialfs.h', 'fs/serialfs/sfs_client.h', 'fs/serialfs/sfs_proto.h', 'fs/vfs.h', 'include/memmap.h', 'include/platform_device.h', 'include/time_units.h', 'include/types.h', 'platform/pc98/device_fs.c', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h', 'tools/tests/dep_dir_a_l06_host.c', 'sdk/kapi.json', 'kapi/kapi_generated.c', 'fs/vfs.c', 'fs/serialfs/serialfs_session.c', 'tools/tests/test_dep_dir_a_l05a.py', 'tools/tests/fixtures/dep_dir_a/l06.json', 'tools/tests/fixtures/dep_dir_a/l05a.json', 'tools/tests/test_shm_user.py', 'tools/tests/kernel_layout.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'include/config.h', 'include/ide_contract.h', 'drivers/ata/ide.h', 'platform/pc98/device_ide.c', 'sdk/include/os32/os32_kapi_slots.h', 'exec/exec_kapi_init.inc', 'sdk/rust/os32api/src/kapi_generated.rs', 'hwdev/generated/hwdev_contract.h', 'tools/tests/dep_dir_a_l05a_host.c', 'exec/exec.c', 'exec/exec_image.inc', 'exec/exec_argv.inc', 'drivers/video/pc98/backend_pegc.c', 'gfx/gfx_core.c', 'init/kernel.c', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shm.c', 'kernel/v86/v86.c', 'kernel/v86/v86_gp_emulate.inc', 'kernel/v86/v86_mem.c', 'kernel/v86/v86_snapshot.c', 'tools/tests/shm_user_host.c', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc'],
    needs=['make:all'], budget=60.0)

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
COPIES = (
    'fs/ext2/ext2.h', 'fs/serialfs/serialfs.h', 'fs/serialfs/sfs_client.h', 'fs/serialfs/sfs_proto.h', 'fs/vfs.h',
    'include/memmap.h', 'include/platform_device.h', 'include/time_units.h',
    'include/types.h', 'platform/pc98/device_fs.c',
    'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h',
    'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ls.h',
    'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h',
    'tools/tests/dep_dir_a_l06_host.c',
)
OLD = 'return ext2_format_at(drv, start_lba, length);'
NEW = 'return ext2_format_at(drv, length, start_lba);'


def contracts():
    fixture = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l06.json').read_text())
    abi = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l05a.json').read_text())
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    assert digest(json.dumps(public_spec(spec), sort_keys=True).encode()) == abi['public_sha256']
    for path, expected in abi['generated'].items():
        assert digest((ROOT / path).read_bytes()) == expected, path
    generated = (ROOT / 'kapi/kapi_generated.c').read_text()
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
    assert '#include "kbd.h"' not in (ROOT / 'fs/vfs.c').read_text()
    session = (ROOT / 'fs/serialfs/serialfs_session.c').read_text()
    assert '#include "lan_contract.h"' in session and '"e82557.h"' not in session
    print('PASS fixed baseline public API/generated ABI and exact wrapper guards; A3 includes')
    return wrappers


def build(folder, body, wrappers):
    for rel in COPIES:
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dest)
    (folder / 'platform/pc98/device_fs.c').write_text(body)
    (folder / 'wrappers.inc').write_text(wrappers)
    exe = folder / 'fs-test'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-fno-stack-protector',
                    *include_flags(folder),
                    str(folder / 'platform/pc98/device_fs.c'),
                    str(folder / 'tools/tests/dep_dir_a_l06_host.c'), '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    wrappers = contracts()
    body = (ROOT / 'platform/pc98/device_fs.c').read_text()
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
