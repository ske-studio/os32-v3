#!/usr/bin/env python3
CHECK = dict(env=['socket'], name='ppc-lan-p23', tier=1, budget=60, needs=[],
             covers=['tools/host_tree_manifest.py', 'tools/ppc_link_transport.py', 'tools/remote_transport.py', 'tools/host_agent_config.py','tools/ppc_hostfs.py', 'tools/lanfs_host.py', 'tools/serialfs_host.py', 'tools/lan_bridge.py', 'tools/host_image.py', 'tools/ppc_lan_host.py', 'tools/ppc/test_lan_p23.py', 'sys/platform/powermac/lan_wire.c'],
    basis=['PPC_LAN_DESIGN §3'],
    pins=['LAN_OPERATOR_DESIGN'],
    env_limit='hostのwire/LOG模型を確認するが、実LANの損失・順序と実NICの障害は保証しない。',
)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/ppc/test_lan_p23.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
