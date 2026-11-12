"""PowerMac USB serial acceptance and optional LAN RPC-stall regression."""
CHECK = dict(name='ppc-usb', tier=2, budget=900, needs=['make:all'],
             covers=['tools/checks/ppc-usb.py', 'tools/ppc/usb_run.py',
                     'platform/powermac/usb*', 'build/ppc-kboot.mk', 'init/ppc_kernel.c',
                     'platform/powermac/kboot_capture.c', 'platform/powermac/terminal_input.c',
                     'drivers/usb/*.[ch]', 'include/usb_platform.h', 'include/endian_dma.h',
                     'arch/ppc/arch_endian_dma.h', 'tools/ppc/kboot_accept.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/run.py',
                     'platform/powermac/shell_boot.c', 'platform/powermac/input_boot.c',
                     'arch/ppc/entry.S',
                     'arch/ppc/kernel_dec.c',
                     'arch/ppc/kernel_mmu.S',
                     'arch/ppc/kernel_probe.S',
                     'arch/ppc/kernel_trap.c',
                     'arch/ppc/ofw_note.S',
                     'platform/powermac/boot.c',
                     'platform/powermac/clock.c',
                     'platform/powermac/escc.c',
                     'platform/powermac/escc_tree.c',
                     'platform/powermac/mpic.c',
                     'platform/powermac/ofw.c',
                     'tools/host_image.py',
                     'tools/lan_bridge.py',
                     'tools/lanfs_host.py',
                     'tools/ppc/kboot_mutations.py',
                     'tools/ppc_hostfs.py',
                     'tools/ppc_lan_host.py',
                     'tools/serialfs_host.py', 'tools/ppc/sd_run.py',
                     'arch/ppc/kernel_boot.h'])


def run(ctx):
    import os
    import subprocess
    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    return subprocess.call(['python3', '-B', 'tools/ppc/usb_run.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
