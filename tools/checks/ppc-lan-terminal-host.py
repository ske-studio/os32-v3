import subprocess

CHECK = dict(name='ppc-lan-terminal-host', tier=1, budget=60, needs=[],
             covers=['tools/host_tree_manifest.py', 'tools/ppc_link_transport.py', 'tools/remote_transport.py', 'tools/host_agent_config.py','tools/ppc_hostfs.py', 'tools/lanfs_host.py', 'tools/serialfs_host.py', 'tools/lan_bridge.py', 'tools/host_image.py', 'tools/ppc_lan_host.py', 'tools/ppc/test_lan_terminal_host.py'],
    basis=['PPC_SHELL_DESIGN §3', 'PPC_SHELL_DESIGN §7'],
    pins=['LAN_OPERATOR_DESIGN'],
    env_limit='hostのTERMとseq/drop処理を確認するが、実端末と実LANの遅延・切断は再現しない。',
)


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/ppc/test_lan_terminal_host.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
