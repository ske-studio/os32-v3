#!/usr/bin/env python3
"""C4 SerialFS deadlines, status mapping and generation hooks with real VFS/C1/C3."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

CHECK = dict(name='hostsvc-serialfs', tier=1, needs=[], budget=10, covers=[
    'fs/serialfs/serialfs_persistent.c', 'fs/serialfs/serialfs.h',
    'fs/serialfs/serialfs.c', 'fs/serialfs/sfs_client.c', 'fs/serialfs/sfs_client.h',
    'fs/serialfs/sfs_proto.c', 'fs/serialfs/sfs_proto.h',
    'fs/vfs.c', 'fs/vfs.h', 'fs/vfs_device.h',
    'net/host_mailbox.c', 'net/host_mailbox.h', 'net/nic.h',
    'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/hostsvc_fixture.c', 'tools/tests/hostsvc_fixture.h',
    'tools/tests/hostsvc_serialfs_host.c', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'build/config.mk',
    'include/types.h', 'include/io.h', 'mm/kmalloc.h',
    'lib/crc32.c', 'lib/crc32.h', 'lib/le_internal.h', 'lib/kstring.h',
    'lib/kstring_asm.asm', 'include/console.h', 'include/memmap.h'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('deadline', '#define SFS_OPERATION_TICKS 100u', '#define SFS_OPERATION_TICKS 101u'),
    ('hostdown', 'case OS32_ERR_HOSTDOWN: case OS32_ERR_TIMEOUT: case OS32_ERR_STALE:',
     'case OS32_ERR_TIMEOUT: case OS32_ERR_STALE:'),
    ('timeout', 'case OS32_ERR_HOSTDOWN: case OS32_ERR_TIMEOUT: case OS32_ERR_STALE:',
     'case OS32_ERR_HOSTDOWN: case OS32_ERR_STALE:'),
    ('stale', 'case OS32_ERR_HOSTDOWN: case OS32_ERR_TIMEOUT: case OS32_ERR_STALE:',
     'case OS32_ERR_HOSTDOWN: case OS32_ERR_TIMEOUT:'),
    ('request', 'rc = sfs_request_deadline(type, pl, len, &reply, deadline);',
     'rc = sfs_request_deadline(type, pl, len, &reply, deadline + 1);'),
    ('read-budget', '&body, &blen, storage, deadline);\n        if (rc < 0) return rc;\n        /* **要求より多く返さない。**',
     '&body, &blen, storage, deadline + (sfs_deadline(c) - deadline));\n        if (rc < 0) return rc;\n        /* **要求より多く返さない。**'),
    ('write-budget', '&body, &blen, storage, deadline);\n        if (rc < 0) return rc;\n        if ((u32)rc != chunk || blen != 0)',
     '&body, &blen, storage, deadline + (sfs_deadline(c) - deadline));\n        if (rc < 0) return rc;\n        if ((u32)rc != chunk || blen != 0)'),
    ('list-budget', '&body, &blen, storage, deadline);\n        if (rc < 0) return rc;\n        if (rc != 0 || blen < SFS_LIST_RSP_COOKIE_LEN)',
     '&body, &blen, storage, deadline + (sfs_deadline(c) - deadline));\n        if (rc < 0) return rc;\n        if (rc != 0 || blen < SFS_LIST_RSP_COOKIE_LEN)'),
    ('generation', '*generation = sfs_persistent_generation();', '*generation = 0;'),
    ('check-ensure', 'int rc = sfs_ensure_session(sfs_deadline(c));', 'int rc = 0;'),
    ('refresh', 'return sfs_resident(c) ? sfs_ensure_session(sfs_deadline(c)) : 0;', '(void)c; return 0;'),
    ('path-length', 'if (rc > 0 || (rc == 0 && blen != 0)) return OS32_ERR_IO;       /*',
     'if (rc > 0) return OS32_ERR_IO;       /*'),
    ('write-length', '(u32)rc != chunk || blen != 0', '(u32)rc != chunk'),
    ('stat-length', 'blen != SFS_STAT_RSP_LEN', 'blen > SFS_STAT_RSP_LEN'),
)



def build(work, source):
    exe = work / 'serialfs'
    subprocess.run(['nasm', '-f', 'elf32', str(ROOT / 'lib/kstring_asm.asm'),
                    '-o', str(work / 'kstring.o')], check=True)
    result = host32.build(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O0',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
        '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
        '-I' + str(ROOT), *include_flags(ROOT),
        *[str(ROOT / p) for p in [
            'tools/tests/hostsvc_serialfs_host.c', 'tools/tests/hostsvc_fixture.c',
            'fs/vfs.c', 'fs/serialfs/sfs_client.c',
            'fs/serialfs/sfs_proto.c', 'lib/crc32.c']],
        str(source), str(work / 'kstring.o'), '-o', str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return host32.run([str(exe)], timeout=30).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = ROOT / 'fs/serialfs/serialfs.c'
    with tempfile.TemporaryDirectory(prefix='hostsvc-serialfs-') as tmp:
        work = Path(tmp)
        cross = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), '-std=gnu11', '-O2', '-Wall', '-Wextra',
            '-Werror', '-ffreestanding', '-I' + str(ROOT), *include_flags(ROOT),
            '-c', str(source), '-o', str(work / 'target.o')], check=True)
        assert build(work, source) == 0, 'normal control failed'
        print('GREEN C4 VFS/LAN/UART/status/deadline/generation/recovery', flush=True)
        if args.mutate:
            original = source.read_text()
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, 'anchor must be unique')
                mutant = work / 'serialfs.c'
                mutant.write_text(original.replace(old, new))
                rc = build(work, mutant)
                assert rc > 0, (name, 'expected runtime RED', rc)
                print(f'RED {name} runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_serialfs.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
