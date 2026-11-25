CHECK = dict(name='ppc-partition-host', tier=1, budget=15, needs=[],
             covers=['tools/tests/test_ppc_partition.py', 'tools/tests/ppc_partition_host.c',
                     'tools/tests/host32.py', 'tools/tests/cc_cache.py',
                     'sys/platform/powermac/install_backend.h', 'sys/platform/powermac/product_boot.h', 'platform/powermac/apm.*', 'sys/platform/powermac/partition_view.c',
                     'sys/platform/powermac/platform_ext2.h', 'lib/kstring*', 'sys/hal/block/dev.h',
                     'include/**', 'fs/ext2/**', 'platform/powermac/root_boot.*', 'fs/*.h',
                     'sys/kern/module/module.h', 'sys/kern/dev/dev_name.h', 'sys/kern/exec/exec_hooks.h', 'lib/path.h', 'sys/dev/ata/ide.h', 'sdk/include/**'],
             basis=['docs/decisions/ppc/PPC_ROOT_MOUNT_ALIGN_X86_DESIGN.md §4'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_ppc_partition.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
