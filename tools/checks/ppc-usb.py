"""PowerMac USB serial acceptance and optional LAN RPC-stall regression."""
CHECK = dict(env=['ppc-cross', 'qemu-ppc'], name='ppc-usb', tier=2, budget=900, needs=['make:ppc-ci-image'],
             covers=['sys/arch/ppc/exec_enter.S', 'sys/kern/exec/exec.c', 'tools/ppc/ppc_shell_flow.inc', 'sys/platform/powermac/shell_loop_ops.c', 'userland/shell/main.c', 'userland/shell/rshell.c', 'tools/host_tree_manifest.py', 'tools/ppc/link_diag_run.py',
        'sys/fs/serialfs/serialfs_persistent.c',
        'sys/kern/console/con_sink.c',
        'sys/kern/console/console.c',
        'sys/kern/mm/kmalloc.c',
        'tools/host_agent.py',
        'tools/host_linux.py',
        'tools/host_notify.py',
        'tools/host_operator_socket.py',
        'tools/host_rshell.py',
        'tools/host_services.py',
        'tools/host_sfs_link.py',
        'tools/ppc/link_run.py',
        'tools/ppc/link_sh_run.py',
        'userland/shell/rshell_lan.c',
        'tools/ppc_link_transport.py',
        'tools/remote_transport.py',
        'tools/host_agent_config.py','sys/platform/powermac/console_stream.c',
        'sys/platform/powermac/log_printf.c',
                     'sys/platform/powermac/log.c', 'sys/platform/powermac/kboot_log.h', 'sys/platform/powermac/input_usb.c', 'sys/platform/powermac/input_keymap_backend.c', 'sys/platform/powermac/input_usb.h', 'tools/tests/host32.py', 'tools/checks/ppc-usb.py', 'tools/ppc/usb_run.py',
                     'platform/powermac/usb*', 'build/targets/ppc-powermac.mk', 'sys/platform/powermac/ppc_kernel.c',
                     'sys/platform/powermac/kboot_capture.c', 'sys/platform/powermac/terminal_fifo.inc', 'sys/platform/powermac/terminal_input.c',
                     'drivers/usb/*.[ch]', 'sys/include/usb_platform.h', 'sys/include/endian_dma.h',
                     'sys/arch/ppc/arch_endian_dma.h', 'tools/ppc/kboot_accept.py', 'tools/ppc/boot_media.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/run.py',
                     'sys/platform/powermac/shell_boot.c', 'sys/platform/powermac/input_boot.c',
                     'sys/arch/ppc/entry.S',
                     'sys/arch/ppc/kernel_dec.c',
                     'sys/arch/ppc/kernel_mmu.S',
                     'sys/arch/ppc/kernel_probe.S',
                     'sys/arch/ppc/kernel_trap.c',
                     'sys/arch/ppc/ofw_note.S',
                     'sys/platform/powermac/boot.c',
                     'sys/platform/powermac/clock.c',
                     'sys/platform/powermac/escc.c',
                     'sys/platform/powermac/escc_tree.c',
                     'sys/platform/powermac/mpic.c',
                     'sys/platform/powermac/ofw.c',
                     'tools/host_image.py',
                     'tools/lan_bridge.py',
                     'tools/lanfs_host.py',
                     'tools/ppc/kboot_mutations.py',
                     'tools/ppc_hostfs.py',
                     'tools/ppc_lan_host.py',
                     'tools/serialfs_host.py', 'tools/ppc/sd_run.py',
                     'sys/arch/ppc/kernel_boot.h'],
    # W4 landed: the STALL scene now asserts AC-CTX-1/6 (90..110 ticks), so it no longer pins.
    basis=['USB_DESIGN §4', 'USB_DESIGN §5', 'USB_DESIGN §6', 'USB_DESIGN §7', 'AC-CTX-1', 'AC-CTX-6'],
    env_limit='QEMUのUSBとUSER syscallを確認するが、実USB機器・HC・IRQの遅延やSTALLは再現しない。',
)


def run(ctx):
    import os
    import subprocess
    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    return subprocess.call(['python3', '-B', 'tools/ppc/usb_run.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
