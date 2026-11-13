"""QEMU INDEX8 shell pixel acceptance with product runtime mutations."""
CHECK = dict(name='ppc-fb', tier=2, budget=900, needs=['make:kernel'], covers=[
    'tools/checks/ppc-fb.py', 'tools/ppc/fb_run.py', 'tools/ppc/fb_pixel.py',
    'tools/ppc/usb_run.py', 'tools/ppc/sd_run.py', 'tools/ppc/kboot_accept.py',
    'tools/ppc/kboot_run.py', 'tools/ppc/kboot_mutations.py', 'tools/ppc/run.py',
    'tools/ppc_lan_host.py', 'tools/ppc_hostfs.py', 'tools/lanfs_host.py',
    'tools/serialfs_host.py', 'tools/host_image.py', 'tools/lan_bridge.py',
    'tools/gen_memmap.py', 'sdk/gen_kapi.py', 'tools/gen_font16.py',
    'tools/gen_g16.py', 'tools/gen_unicode.py', 'tools/mkroot_image.py',
    'build/ppc-kboot.mk', 'build/ppc-user.mk', 'build/ppc-ram-root.json',
    'build/ram-root.mk', 'init/ppc_kernel.c', 'arch/ppc/**',
    'platform/powermac/**', 'kernel/console/**', 'kernel/irq/tick.c',
    'kernel/time/tick.c', 'kernel/sys.c', 'drivers/video/of_fb/**',
    'drivers/video/kcg/**', 'drivers/usb/**', 'gfx/**', 'fs/**', 'mm/**',
    'lib/**', 'include/**', 'sdk/**', 'userland/**', 'assets/fonts/**',
    'tools/fetch_fonts.py', 'requirements.txt', 'arch/x86/kernel/kentry.asm',
    'boot/loader_fat_new.asm', 'build/core.yaml', 'build/os32.ld',
    'tools/check_artifacts.py', 'tools/deploy_manifests.py', 'tools/mkpkg.py',
    'tools/mkshlib.py', 'tools/tests/kernel_includes.py',
    'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py'])


def run(ctx):
    import os
    import subprocess
    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    return subprocess.call(['python3', '-B', 'tools/ppc/fb_run.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
