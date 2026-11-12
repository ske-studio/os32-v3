"""Register the PowerPC fd1 stream acceptance."""
CHECK = dict(name='ppc-sh-output', tier=2, budget=900, needs=['make:all'],
             covers=['tools/ppc/sc_run.py', 'userland/ppc/nest.c',
                     'tools/ppc/ppc_sh_output.py', 'userland/ppc/outprobe.c',
                     'platform/powermac/kboot_console.c', 'platform/powermac/console_stream.c',
                     'fs/vfs_fd.c',
                     'build/ppc-user.mk', 'build/ppc-kboot.mk',
                     'tools/ppc/sb_run.py', 'tools/ppc/kboot_accept.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/run.py',
                     'tools/ppc_lan_host.py', 'tools/mkroot_image.py',
                     'platform/powermac/cli_boot.c', 'arch/ppc/exec_context.c',
                     'arch/ppc/kapi_call.c',
                     'userland/ppc/hello.c', 'build/out/ppc-user/**',
                     'arch/ppc/entry.S', 'arch/ppc/kernel_dec.c',
                     'arch/ppc/kernel_mmu.S', 'arch/ppc/kernel_probe.S',
                     'arch/ppc/kernel_trap.c', 'arch/ppc/ofw_note.S',
                     'init/ppc_kernel.c', 'platform/powermac/boot.c',
                     'platform/powermac/clock.c', 'platform/powermac/escc.c',
                     'platform/powermac/escc_tree.c', 'platform/powermac/mpic.c',
                     'platform/powermac/ofw.c', 'tools/ppc/kboot_mutations.py',
                     'tools/ppc/shell_run.py'])


def run(ctx):
    import os
    import subprocess
    import shutil
    from pathlib import Path
    required = ("/home/hight/opt/cross-ppc/bin/powerpc-elf-gcc", "qemu-system-ppc", "genisoimage")
    if any(shutil.which(tool) is None for tool in required) or not Path("/usr/share/qemu/openbios-ppc").is_file():
        print("SKIP ppc-sh-output: PPC toolchain/QEMU/OpenBIOS unavailable")
        return 0
    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    rc = subprocess.call(['python3', '-B', 'tools/ppc/ppc_sh_output.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)

    return 0 if rc == 3 else rc
