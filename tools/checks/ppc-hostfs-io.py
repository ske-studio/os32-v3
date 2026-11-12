#!/usr/bin/env python3
"""Register W8 timebase and IRQ-aware HostFS transport contracts."""
import subprocess

CHECK = dict(name='ppc-hostfs-io', tier=1, budget=30, needs=[],
             covers=['platform/powermac/hostfs_io.c', 'platform/powermac/hostfs.h',
                     'platform/powermac/hostfs_peer.c', 'platform/powermac/hostfs_boot.c',
                     'arch/ppc/kernel_trap.c', 'tools/tests/ppc_hostfs_io.c',
                     'tools/tests/ppc_hostfs_io.py', 'arch/ppc/pd_walk.c',
                     'fs/serialfs/sfs_client.c', 'fs/serialfs/sfs_proto.c',
                     'fs/serialfs/serialfs.c', 'fs/serialfs/serialfs_persistent.c',
                     'fs/vfs.c', 'fs/vfs_fd.c', 'lib/kstring_c.c',
                     'lib/crc32.c', 'lib/divdi3.c'])


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/ppc_hostfs_io.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
