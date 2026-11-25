#!/usr/bin/env python3
"""PACK PM decision: boot-token nonce prevents restart write loss.
Expected replay contract: tools/lanfs_host.py:63-65 (same peer + nonce).
"""
import sfs_restart_fixture as fixture
CHECK = dict(name='sfs-restart-link', tier=1, needs=[], budget=5,
    covers=['sys/fs/serialfs/serialfs_persistent.c', 'sys/fs/serialfs/sfs_proto.c', 'lib/crc32.c', 'lib/crc32_core.inc', 'lib/crc32.h', 'lib/le_internal.h', 'fs/serialfs/*.h', 'include/*.h', 'sys/net/host_mailbox.h', 'sys/net/nic.h', 'fs/*.h', 'sdk/include/os32/*.h', 'tools/host_agent.py', 'tools/host_sfs_link.py', 'tools/host_*.py', 'tools/lan*.py', 'tools/serialfs_host.py', 'tools/tests/sfs_boot_nonce_host.c', 'tools/tests/sfs_restart_fixture.py', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk'],
    basis=['PACK.md PM decision (ppc-sfs-nonce)', 'tools/lanfs_host.py HELLO replay rule'])
def run(ctx):
    import os
    import subprocess
    import sys
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', __file__]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    fixture.main(True)
