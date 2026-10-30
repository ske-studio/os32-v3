#!/usr/bin/env python3
CHECK = dict(name='hwdev-module-host', tier=1,
    covers=['kernel/module/hwdev_module.c', 'kernel/module/hwdev_module.h', 'kernel/module/module.c', 'kernel/module/module.h', 'kernel/module/module_runtime.c', 'kernel/module/module_runtime.h', 'kernel/module/module_image.c', 'kernel/module/module_image.h', 'kernel/module/module_schema.h', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'tools/gen_hwdev_contract.py', 'tools/hwdev_layout.json', 'hwdev/generated/*', 'include/hwdev_private.h', 'kdebug/recover*.h', 'kdebug/gfinit*.h', 'kdebug/kpeek.h', 'tools/tests/hwdev_fixture.c', 'tools/tests/hwdev_module_host.c', 'tools/tests/test_hwdev_module.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/mkmod.py', 'docs/decisions/realhw/HWDEV_CORE_API.md', 'build/checks.d/check-hwdev-module-host.mk', 'tools/check_map.d/check-hwdev-module-host.yaml', 'tools/gen_kdebug_imports.py', 'kernel/module/kdebug_imports.def', 'sdk/kapi.json', 'lib/crc32_core.inc', 'build/module.ld', 'hwdev/hwdev_imports.def', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=3.181738)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/gen_hwdev_contract.py', '--check', '--cc', *shlex.split(ctx.make_vars['CROSS_DIR'] + '/bin/i386-elf-gcc'), '--flags', ' '.join(x for x in ctx.make_vars['KERNEL_CFLAGS'].split() if x not in ('-MMD', '-MP')), '--includes', ctx.make_vars['INC_KERNEL']]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_hwdev_module.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
