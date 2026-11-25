#!/usr/bin/env python3
CHECK = dict(name='hwdev-module-host', tier=1,
    covers=['sys/platform/powermac/module_diag.c', 'sys/platform/powermac/diag_slots.h', 'sys/kern/kdebug/hwb_wire.h', 'sys/kern/kdebug/probe_wire.h', 'sys/kern/kdebug/probe_validate.h', 'sys/include/debug_probe_snapshot.h', 'sys/include/debug_probe.h', 'sys/platform/pc98/module_pair.c', 'sys/include/platform_module_pair.h', 'sys/kern/module/debug_probe.c', 'sys/dev/hwdev/hwdev_module.c', 'sys/dev/hwdev/hwdev_module.h', 'sys/kern/kdebug/kdebug_module.c', 'sys/kern/module/module.h', 'sys/kern/module/module_runtime.c', 'sys/kern/module/module_runtime.h', 'sys/kern/module/module_image.c', 'sys/kern/module/module_image.h', 'sys/kern/module/module_schema.h', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'tools/gen_hwdev_contract.py', 'tools/hwdev_layout.json', 'hwdev/generated/*', 'sys/dev/hwdev/hwdev_private.h', 'kdebug/recover*.h', 'kdebug/gfinit*.h', 'sys/platform/pc98/kdebug/kpeek.h', 'tools/tests/hwdev_fixture.c', 'tools/tests/hwdev_module_host.c', 'tools/tests/test_hwdev_module.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/mkmod.py', 'docs/design/hwdev/hwdev_imports.md', 'tools/gen_kdebug_imports.py', 'sys/kern/module/kdebug_imports.def', 'sdk/kapi.json', 'build/module.ld', 'sys/dev/hwdev/hwdev_imports.def', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=4.6,
    kind='characterization',
)


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
