CHECK = dict(name='nofpu-host', tier=1, budget=12,
    covers=['tools/tests/test_nofpu.py', 'tools/tests/test_packages.py',
            'assets/filetypes', 'fs/pc98pt.c', 'fs/ext2/ext2_layout.c',
            'tools/check_artifacts.py', 'tools/tests/install_fresh_host.c',
            'userland/shell/hdprep_plan.c', 'userland/system/cdinst.c',
            'userland/system/inst_disk.c', 'userland/system/inst_hdd.c',
            'userland/system/install.c', 'tools/deploy_manifests.py', 'tools/mkpkg.py', 'build/core.yaml',
            'build/packages.yaml', 'userland/deploy.yaml',
            'build/softfloat.mk', 'build/features/x87.mk', 'build/sources/order.mk',
            'build/kernel.mk', 'build/config.mk', 'build/out/**', 'images/**',
            'lib/softfloat/COPYING.txt', 'arch/x86/kernel/x87_emu_vendor/LICENSE-I386C-FPU.TXT',
            'userland/tests/fp_stop.c', 'tools/tests/guest_tests.txt'],
    needs=['make:all', 'make:kernel-testnofpu'], basis=['[L1]'])


def run(ctx):
    import subprocess
    import sys
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_nofpu.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
