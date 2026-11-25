#!/usr/bin/env python3
"""Register W8 timebase and IRQ-aware HostFS transport contracts."""
import subprocess

CHECK = dict(name='ppc-hostfs-io', tier=1, budget=30, needs=[],
             covers=['sys/platform/powermac/hostfs_io.c', 'sys/platform/powermac/hostfs.h',
                     'sys/platform/powermac/hostfs_peer.c', 'sys/platform/powermac/hostfs_boot.c',
                     'sys/arch/ppc/kernel_trap.c', 'tools/tests/ppc_hostfs_io.c',
                     'tools/tests/ppc_hostfs_io.py', 'sys/arch/ppc/pd_walk.c',
                     'sys/fs/serialfs/sfs_client.c', 'sys/fs/serialfs/sfs_proto.c',
                     'sys/fs/serialfs/serialfs.c', 'sys/fs/serialfs/serialfs_persistent.c', 'sys/include/boot_token.h',
                     'sys/fs/vfs.c', 'sys/fs/vfs_fd.c', 'sys/kern/kstring_c.c',
                     'lib/crc32.c', 'lib/divdi3.c'],
    basis=['AC-SFS-1'],
    pins=['LAN_OPERATOR_DESIGN', 'LAN_BULK_DESIGN'],
)


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/ppc_hostfs_io.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
