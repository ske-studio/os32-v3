"""Link S-D is full-only: routine QEMU acceptance time stays unchanged.

icount is only for USB typing tests; HostFS acceptance runs without icount."""
CHECK = dict(name='ppc-sd-link', env=['ppc-cross', 'qemu-ppc'], tier=2,
    needs=['make:ppc-ci-image'], budget=1800, full_only=True,
    covers=['tools/checks/ppc-sd-link.py', 'tools/tests/test_ppc_sd_run.py',
            'tools/ppc/**', 'tools/*.py', 'build/ppc-kboot.mk', 'build/ppc-user.mk',
            'fs/serialfs/*.[ch]', 'platform/powermac/**', 'arch/ppc/**',
            'userland/shell/rshell_lan.c', 'kernel/console/**', 'mm/kmalloc.c',
            'platform/powermac/ppc_kernel.c'],
    basis=['LAN_OPERATOR_DESIGN §5 P5-sd/fs'],
    pins=['LAN_OPERATOR_DESIGN', 'LAN_BULK_DESIGN'],
    env_limit='mac99 QEMU link only; denied UDP/TCP sockets are unverified, never PASS.')


def run(ctx):
    import os
    import subprocess
    rc = subprocess.call(['python3', '-B', 'tools/ppc/sd_run.py', '--link',
                          *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
    return 0 if rc == 3 and os.environ.get('OS32_REQUIRE_PPC') != '1' else rc
