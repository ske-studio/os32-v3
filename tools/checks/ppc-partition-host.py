CHECK = dict(name='ppc-partition-host', tier=1, budget=15, needs=[],
             covers=['tools/tests/test_ppc_partition.py', 'tools/tests/ppc_partition_host.c',
                     'tools/tests/host32.py', 'tools/tests/cc_cache.py',
                     'platform/powermac/product_boot.h', 'platform/powermac/apm.*', 'platform/powermac/partition_view.c',
                     'platform/powermac/platform_ext2.h', 'lib/kstring*', 'kernel/dev/dev.h',
                     'include/**', 'fs/ext2/**', 'platform/powermac/root_boot.*', 'fs/*.h',
                     'kernel/module/module.h', 'kernel/dev/dev_name.h', 'exec/exec_hooks.h', 'lib/path.h', 'drivers/ata/ide.h', 'sdk/include/**'],
             basis=['docs/decisions/ppc/PPC_ROOT_MOUNT_ALIGN_X86_DESIGN.md §4'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_ppc_partition.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
