"""Register S-D LAN acceptance; sandbox without AF_INET explicitly skips."""
CHECK = dict(env=['ppc-cross', 'qemu-ppc'], name='ppc-sd', tier=2, budget=900, needs=['make:ppc-ci-image'],
             covers=['sys/arch/ppc/exec_enter.S', 'sys/kern/exec/exec.c', 'tools/ppc/ppc_shell_flow.inc', 'sys/platform/powermac/shell_loop_ops.c', 'userland/shell/main.c', 'userland/shell/rshell.c', 'tools/host_tree_manifest.py', 'tools/ppc/link_diag_run.py',
        'sys/kern/console/con_sink.c',
        'sys/kern/console/console.c',
        'sys/kern/mm/kmalloc.c',
        'sys/platform/powermac/usb_boot.c',
        'sys/platform/powermac/usb_platform.c', 'sys/platform/powermac/usb_key_report.c',
        'tools/host_agent.py',
        'tools/host_linux.py',
        'tools/host_notify.py',
        'tools/host_operator_socket.py',
        'tools/host_rshell.py',
        'tools/host_services.py',
        'tools/host_sfs_link.py',
        'tools/ppc/link_run.py',
        'tools/ppc/link_sh_run.py',
        'tools/ppc/usb_run.py',
        'userland/shell/rshell_lan.c',
        'tools/ppc_link_transport.py',
        'tools/remote_transport.py',
        'tools/host_agent_config.py','sys/arch/ppc/entry.S',
                     'sys/arch/ppc/exec_context.c',
                     'sys/arch/ppc/kapi_call.c',
                     'sys/arch/ppc/kernel_dec.c',
                     'sys/arch/ppc/kernel_mmu.S',
                     'sys/arch/ppc/kernel_probe.S',
                     'sys/arch/ppc/kernel_trap.c',
                     'sys/arch/ppc/ofw_note.S',
                     'build/core.yaml',
                     'build/out/ppc-user/**',
                     'build/targets/ppc-powermac.mk',
                     'build/ppc-user.mk',
                     'fs/serialfs/*.[ch]',
                     'sys/platform/powermac/ppc_kernel.c',
                     'sys/platform/powermac/boot.c',
                     'sys/platform/powermac/cli_boot.c',
                     'sys/platform/powermac/clock.c',
                     'sys/platform/powermac/console_stream.c',
                     'sys/platform/powermac/escc.c',
                     'sys/platform/powermac/escc_tree.c',
                     'platform/powermac/hostfs*',
                     'sys/platform/powermac/input_boot.c',
                     'sys/platform/powermac/input_decode.c',
                     'sys/platform/powermac/kboot_console.c',
                     'sys/platform/powermac/mpic.c',
                     'sys/platform/powermac/ofw.c',
                     'sys/platform/powermac/shell_boot.c',
                     'tools/checks/ppc-sd.py',
                     'tools/deploy_manifests.py',
                     'tools/host_image.py',
                     'tools/lan_bridge.py',
                     'tools/lanfs_host.py',
                     'tools/mkpkg.py',
                     'tools/mkroot_image.py',
                     'tools/ppc/kboot_accept.py', 'tools/ppc/boot_media.py',
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
                     'userland/ppc/nest.c'],
    basis=['PPC_SHELL_W8_DESIGN §3', 'PPC_SHELL_W8_DESIGN §6'],
    pins=['LAN_OPERATOR_DESIGN', 'LAN_BULK_DESIGN'],
    env_limit='QEMUのLAN永続化を確認するが、実NIC・媒体の障害は再現せず、AF_INET不可時のSKIPは受入成功に数えない。',
)


def run(ctx):
    import os
    import shutil
    import subprocess
    from pathlib import Path
    required = (str(Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'),
                'qemu-system-ppc', 'genisoimage')
    if any(shutil.which(x) is None for x in required) or not Path('/usr/share/qemu/openbios-ppc').is_file():
        missing = 'PPC toolchain' if shutil.which(required[0]) is None else 'QEMU/OpenBIOS/genisoimage'
        print('SKIP ppc-sd: ' + missing + ' unavailable')
        return int(os.environ.get('OS32_REQUIRE_PPC') == '1')
    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    env['CROSS_DIR'] = env.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')
    return subprocess.call(['python3', '-B', 'tools/ppc/sd_run.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
