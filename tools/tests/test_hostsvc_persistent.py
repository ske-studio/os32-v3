#!/usr/bin/env python3
"""C1 resident mount/session with real VFS, SFS and non-consuming mailbox."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

CHECK = dict(name='hostsvc-persistent', tier=1, needs=[], budget=10, covers=[
    'fs/serialfs/serialfs_persistent.c', 'fs/serialfs/serialfs.h',
    'fs/serialfs/serialfs.c', 'fs/serialfs/sfs_client.c', 'fs/serialfs/sfs_client.h',
    'fs/serialfs/sfs_proto.c', 'fs/serialfs/sfs_proto.h',
    'fs/vfs.c', 'fs/vfs.h', 'fs/vfs_device.h',
    'net/host_mailbox.c', 'net/host_mailbox.h', 'net/nic.h',
    'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/hostsvc_fixture.c', 'tools/tests/hostsvc_fixture.h',
    'tools/tests/hostsvc_persistent_host.c', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'build/config.mk',
    'include/types.h', 'include/io.h', 'mm/kmalloc.h',
    'lib/crc32.c', 'lib/crc32.h', 'lib/le_internal.h', 'lib/kstring.h',
    'lib/kstring_asm.asm', 'include/console.h', 'include/memmap.h'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('scope-ensure', 'int rc = sfs_ensure_session(config.io->now(config.io_ctx) + SFS_SCOPE_TICKS);',
     'int rc = 0;'),
    ('scope-generation', 'vfs_resource_stale_mount(&resident.client);',
     '(void)0;'),
    ('mount-gate', 'serialfs_permit(&resident.client);', 'serialfs_forbid();'),
    ('mount-conflict', 'return OS32_ERR_BUSY;\n    config = *cfg;',
     'return OS32_ERR_IO;\n    config = *cfg;'),
    ('scope-pair', 'if (scope) return OS32_ERR_BUSY;', 'if (scope) return OS32_ERR_INVAL;'),
    ('generation', 'resident.generation++;', 'resident.generation += 0;'),
    ('same-alive', 'host_generation != generation', 'host_generation == generation'),
    ('snapshot', 'resident.alive_seen = alive->observation;',
     'resident.alive_seen = alive->observation + 1;'),
    ('idle', '>= SFS_PERSISTENT_IDLE_TICKS', '> SFS_PERSISTENT_IDLE_TICKS'),
    ('seq', 'c->seq > SFS_PERSISTENT_SEQ_LIMIT;', 'c->seq > SFS_PERSISTENT_SEQ_LIMIT + 1;'),
    ('bye', '(void)sfs_client_oneway_deadline(c, SFS_T_BYE, 0, 0, deadline);', '(void)0;'),
    ('bye-deadline', 'sfs_client_oneway_deadline(c, SFS_T_BYE, 0, 0, deadline)',
     'sfs_client_oneway(c, SFS_T_BYE, 0, 0)'),
    ('bye-deadline-forward', 'sfs_client_oneway_deadline(c, SFS_T_BYE, 0, 0, deadline)',
     'sfs_client_oneway_deadline(c, SFS_T_BYE, 0, 0, deadline + 1)'),
    ('deadline', 'if ((i32)(now - deadline) >= 0)', 'if ((i32)(now - deadline) > 0)'),
    ('deadline-forward', 'sfs_client_hello_deadline(c, nonce, deadline)',
     'sfs_client_hello_deadline(c, nonce, deadline + 1)'),
    ('retry', 'c->sid = 0;\n        return rc;', 'c->sid = 123;\n        return rc;'),
)


def build(work, source):
    exe = work / 'persistent'
    subprocess.run(['nasm', '-f', 'elf32', str(ROOT / 'lib/kstring_asm.asm'),
                    '-o', str(work / 'kstring.o')], check=True)
    result = host32.build(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O0',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
        '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
        '-I' + str(ROOT), *include_flags(ROOT),
        '-DPRODUCT_SOURCE="' + str(source) + '"',
        *[str(ROOT / p) for p in [
            'tools/tests/hostsvc_persistent_host.c', 'tools/tests/hostsvc_fixture.c',
            'fs/vfs.c', 'fs/serialfs/serialfs.c', 'fs/serialfs/sfs_client.c',
            'fs/serialfs/sfs_proto.c', 'lib/crc32.c']],
        str(work / 'kstring.o'), '-o', str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return host32.run([str(exe)], timeout=30).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = ROOT / 'fs/serialfs/serialfs_persistent.c'
    with tempfile.TemporaryDirectory(prefix='hostsvc-persistent-') as tmp:
        work = Path(tmp)
        cross = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), '-std=gnu11', '-O2', '-Wall', '-Wextra',
            '-Werror', '-ffreestanding', '-I' + str(ROOT), *include_flags(ROOT),
            '-c', str(source), '-o', str(work / 'target.o')], check=True)
        assert build(work, source) == 0, 'normal control failed'
        print('GREEN C1 mount/scope/idle/ALIVE/generation/seq/deadline/recovery', flush=True)
        if args.mutate:
            original = source.read_text()
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, 'anchor must be unique')
                mutant = work / 'serialfs_persistent.c'
                mutant.write_text(original.replace(old, new))
                rc = build(work, mutant)
                assert rc == 1, (name, 'expected runtime RED', rc)
                print('RED ' + name + ' runtime rc=1', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_persistent.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
