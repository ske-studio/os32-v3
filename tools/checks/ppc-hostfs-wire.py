#!/usr/bin/env python3
"""Register the PLN1 external-payload wire test (W8 T01)."""
import subprocess

CHECK = dict(name='ppc-hostfs-wire', tier=1, budget=30, needs=[],
             covers=['platform/powermac/lan_wire.c', 'platform/powermac/lan_boot.h',
                     'tools/ppc_lan_host.py', 'tools/tests/ppc_hostfs_wire.py',
                     'lib/kstring_c.c', 'tools/host_image.py',
                     'tools/lan_bridge.py', 'tools/lanfs_host.py',
                     'tools/ppc_hostfs.py', 'tools/serialfs_host.py'])


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/ppc_hostfs_wire.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
