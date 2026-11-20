"""Heavy link-SH QEMU acceptance; routine CI time is unchanged."""
CHECK = dict(name='ppc-link-sh-run', env=['ppc-cross','qemu-ppc'], tier=2,
    budget=2400, needs=['make:ppc-ci-image'], full_only=True,
    covers=['exec/exec.c', 'tools/accept/ppc_shell_flow.inc', 'userland/shell/main.c', 'userland/shell/rshell.c', 'fs/serialfs/serialfs_persistent.c', 'tools/ppc/link_sh_run.py', 'tools/tests/test_ppc_link_sh_run.py',
            'build/targets/ppc-powermac.mk','build/ppc-user.mk','userland/shell/rshell_lan.c',
            'platform/powermac/**','arch/ppc/**','kernel/console/**','mm/kmalloc.c',
            'tools/host_agent.py','tools/host_operator_socket.py','tools/host_rshell.py',
            'tools/ppc_link_transport.py','tools/remote_transport.py','tools/ppc/**',
            'tools/ppc_lan_host.py','platform/powermac/ppc_kernel.c','tools/*.py'],
    basis=['PPC_P3_DESIGN v2 §6 and appendix A-6 T2/T3/T4'],
    env_limit='AF_INET UDP/TCP and mac99 QEMU only; physical PPC/USB/LAN are not observed.')


def run(ctx):
    import os
    from pathlib import Path
    import subprocess
    out=Path(os.environ.get('TMPDIR','/tmp'))/'link-sh-accept'
    rc=subprocess.call(['python3','-B','tools/ppc/link_sh_run.py','--out',str(out),
                        *(['--mutate'] if ctx.mutate else [])],cwd=ctx.root)
    return 0 if rc==3 and os.environ.get('OS32_REQUIRE_PPC')!='1' else rc
