"""Register N01 guest acceptance."""
CHECK = dict(env=['ppc-cross', 'qemu-ppc'], name='ppc-sc', tier=2, budget=900, needs=['make:ppc-ci-image'],
             covers=['sys/arch/ppc/exec_enter.S', 'sys/kern/exec/exec.c', 'tools/ppc/ppc_shell_flow.inc', 'sys/platform/powermac/shell_loop_ops.c', 'userland/shell/main.c', 'userland/shell/rshell.c', 'tools/ppc/link_diag_run.py', 'tools/ppc/link_run.py', 'tools/host_*.py', 'tools/ppc_link_transport.py', 'tools/remote_transport.py', 'tools/host_agent_config.py','platform/powermac/input_usb.[ch]', 'sys/platform/powermac/input_keymap_backend.c', 'tools/ppc_hostfs.py', 'tools/lanfs_host.py', 'tools/serialfs_host.py', 'tools/lan_bridge.py', 'tools/host_image.py', 'build/core.yaml', 'sys/platform/powermac/input_boot.c', 'sys/platform/powermac/shell_boot.c', 'tools/deploy_manifests.py', 'tools/mkpkg.py', 'userland/deploy.yaml', 'sys/platform/powermac/input_decode.c', 'sys/platform/powermac/console_stream.c', 'sys/platform/powermac/kboot_console.c',
                     'tools/ppc/sc_run.py', 'userland/ppc/nest.c',
                     'build/ppc-user.mk', 'build/targets/ppc-powermac.mk',
                     'tools/ppc/sb_run.py', 'tools/ppc/kboot_accept.py', 'tools/ppc/boot_media.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/run.py',
                     'tools/ppc_lan_host.py', 'tools/mkroot_image.py',
                     'sys/platform/powermac/cli_boot.c', 'sys/arch/ppc/exec_context.c',
                     'sys/arch/ppc/kapi_call.c',
                     'userland/ppc/hello.c', 'build/out/ppc-user/**',
                     'sys/arch/ppc/entry.S', 'sys/arch/ppc/kernel_dec.c',
                     'sys/arch/ppc/kernel_mmu.S', 'sys/arch/ppc/kernel_probe.S',
                     'sys/arch/ppc/kernel_trap.c', 'sys/arch/ppc/ofw_note.S',
                     'sys/platform/powermac/ppc_kernel.c', 'sys/platform/powermac/boot.c',
                     'sys/platform/powermac/clock.c', 'sys/platform/powermac/escc.c',
                     'sys/platform/powermac/escc_tree.c', 'sys/platform/powermac/mpic.c',
                     'sys/platform/powermac/ofw.c', 'tools/ppc/kboot_mutations.py',
                     'tools/ppc/shell_run.py',
                     # sc_run --link imports link_sh_run/sd_run/usb_run (P5 C).
                     'sys/fs/serialfs/serialfs_persistent.c',
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
                     'tools/ppc/sd_run.py',
                     'tools/ppc/usb_run.py',
                     'userland/shell/rshell_lan.c'],
    basis=['PPC_SHELL_SC_DESIGN'],
    env_limit='QEMU mac99のUSER shell/execを確認するが、実機固有のIRQ・cache・LAN障害は保証せず、tool欠落時のSKIPは受入成功に数えない。',
)


def run(ctx):
    import os
    import subprocess
    import shutil
    from pathlib import Path
    # Local prerequisites are optional; CI requires them.
    required = (str(Path(os.environ.get("PPC_CROSS_DIR", "/home/hight/opt/cross-ppc"))/"bin/powerpc-elf-gcc"),
                "qemu-system-ppc", "genisoimage")
    if any(shutil.which(tool) is None for tool in required) or not Path("/usr/share/qemu/openbios-ppc").is_file():
        missing = 'PPC toolchain' if shutil.which(required[0]) is None else 'QEMU/OpenBIOS/genisoimage'
        print('SKIP ppc-sc: ' + missing + ' unavailable')
        return int(os.environ.get('OS32_REQUIRE_PPC') == '1')
    # The outer x86 check make exports its build paths and compiler flags.
    env = dict(os.environ)
    for key in ("BUILD_OUT", "KERNEL_CFLAGS", "INC_KERNEL", "C_STD",
                "SQLITE_PROTO_OUT", "MAKEFLAGS", "MFLAGS", "MAKELEVEL"):
        env.pop(key, None)
    return subprocess.call(["python3", "-B", "tools/ppc/sc_run.py", "--link"] +
                           (["--mutate"] if ctx.mutate else []), cwd=ctx.root, env=env)
