"""Register S-D LAN acceptance; sandbox without AF_INET explicitly skips."""
CHECK = dict(name='ppc-sd', tier=2, budget=900, needs=['make:ppc-boot'],
             covers=['arch/ppc/entry.S',
                     'arch/ppc/exec_context.c',
                     'arch/ppc/kapi_call.c',
                     'arch/ppc/kernel_dec.c',
                     'arch/ppc/kernel_mmu.S',
                     'arch/ppc/kernel_probe.S',
                     'arch/ppc/kernel_trap.c',
                     'arch/ppc/ofw_note.S',
                     'build/core.yaml',
                     'build/out/ppc-user/**',
                     'build/ppc-kboot.mk',
                     'build/ppc-user.mk',
                     'fs/serialfs/*.[ch]',
                     'init/ppc_kernel.c',
                     'platform/powermac/boot.c',
                     'platform/powermac/cli_boot.c',
                     'platform/powermac/clock.c',
                     'platform/powermac/console_stream.c',
                     'platform/powermac/escc.c',
                     'platform/powermac/escc_tree.c',
                     'platform/powermac/hostfs*',
                     'platform/powermac/input_boot.c',
                     'platform/powermac/input_decode.c',
                     'platform/powermac/kboot_console.c',
                     'platform/powermac/mpic.c',
                     'platform/powermac/ofw.c',
                     'platform/powermac/shell_boot.c',
                     'tools/checks/ppc-sd.py',
                     'tools/deploy_manifests.py',
                     'tools/host_image.py',
                     'tools/lan_bridge.py',
                     'tools/lanfs_host.py',
                     'tools/mkpkg.py',
                     'tools/mkroot_image.py',
                     'tools/ppc/kboot_accept.py',
                     'tools/ppc/kboot_mutations.py',
                     'tools/ppc/kboot_run.py',
                     'tools/ppc/run.py',
                     'tools/ppc/sb_run.py',
                     'tools/ppc/sc_run.py',
                     'tools/ppc/sd_run.py',
                     'tools/ppc/shell_run.py',
                     'tools/ppc_hostfs.py',
                     'tools/ppc_lan_host.py',
                     'tools/serialfs_host.py',
                     'userland/deploy.yaml',
                     'userland/ppc/hello.c',
                     'userland/ppc/nest.c'])


def run(ctx):
    import os
    import shutil
    import subprocess
    from pathlib import Path
    required = ('/home/hight/opt/cross-ppc/bin/powerpc-elf-gcc',
                'qemu-system-ppc', 'genisoimage')
    if any(shutil.which(x) is None for x in required) or not Path('/usr/share/qemu/openbios-ppc').is_file():
        print('SKIP ppc-sd: PPC toolchain/QEMU/OpenBIOS unavailable')
        return 0
    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    env['CROSS_DIR'] = '/home/hight/opt/cross-ppc'
    return subprocess.call(['python3', '-B', 'tools/ppc/sd_run.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
