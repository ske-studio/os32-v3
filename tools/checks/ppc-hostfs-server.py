"""Register the PLN1 SerialFS server test."""
import subprocess

CHECK = dict(name='ppc-hostfs-server', tier=1, budget=30, needs=[],
             covers=['tools/ppc_hostfs.py', 'tools/lanfs_host.py', 'tools/serialfs_host.py', 'tools/lan_bridge.py', 'tools/host_image.py', 'tools/ppc_hostfs.py', 'tools/ppc_lan_host.py', 'tools/tests/ppc_hostfs_server.py'])


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/ppc_hostfs_server.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
