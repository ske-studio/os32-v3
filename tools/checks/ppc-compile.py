"""Compile the complete PPC shell image on hosts with a PPC toolchain."""
CHECK = dict(name='ppc-compile', tier=1, budget=180, needs=['make:ppc-ci-image'],
             covers=['tools/ppc/compile_ci.py', 'tools/tests/ppc_ci.py',
                     'tools/ci/build_cross.sh', 'Makefile',
                     'build/core.yaml', 'userland/deploy.yaml',
                     'tools/deploy_manifests.py', 'tools/mkpkg.py', '.github/actions/ppc-setup/action.yml',
                     '.github/workflows/build.yml', '.github/workflows/check.yml',
                     'build/ppc*.mk', 'build/ppc-ram-root.json', 'build/ram-root.mk',
                     'arch/ppc/**', 'platform/powermac/**', 'init/ppc_kernel.c',
                     'userland/ppc/**', 'lib/**', 'include/**', 'sdk/**',
                     'kernel/**', 'mm/**', 'fs/**', 'gfx/**', 'drivers/**',
                     'assets/fonts/**', 'tools/mkroot_image.py', 'tools/gen_unicode.c'])


def run(ctx):
    import subprocess
    rc = subprocess.call(['python3', '-B', 'tools/tests/ppc_ci.py'] +
                         (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
    if rc:
        return rc
    from ppc.compile_ci import run
    return run(ctx.root)
