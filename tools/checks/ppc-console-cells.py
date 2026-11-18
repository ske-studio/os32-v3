"""PowerMac KAPI cell readback via the link-SH console."""
CHECK = dict(name='ppc-console-cells', env=['ppc-cross', 'qemu-ppc', 'socket'],
    tier=2, budget=900, needs=['make:ppc-ci-image'],
    covers=['tools/checks/ppc-console-cells.py', 'tools/accept/ppc_console_cells.py',
            'tools/ppc/**', 'tools/*.py', 'Makefile', 'build/**', 'sdk/**',
            'arch/ppc/**', 'platform/powermac/**', 'userland/**', 'assets/**',
            'drivers/**', 'include/**', 'init/**', 'kernel/**', 'mm/**',
            'exec/**', 'fs/**', 'gfx/**', 'net/**', 'input/**', 'kapi/**', 'lib/**'],
    basis=['docs/KAPI_SPEC.md §4-8',
           'origin/wt/ppc-kapi-fill:docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md §3「4a」・§5「console_font」'],
    env_limit='Requires mac99 QEMU, OpenBIOS, and AF_INET UDP/TCP.')


def run(ctx):
    import os
    import subprocess

    cmd = ['python3', '-B', 'tools/accept/ppc_console_cells.py']
    if ctx.mutate:
        cmd.append('--mutate')
    rc = subprocess.call(cmd, cwd=ctx.root)
    return 0 if rc == 3 and os.environ.get('OS32_REQUIRE_PPC') != '1' else rc
