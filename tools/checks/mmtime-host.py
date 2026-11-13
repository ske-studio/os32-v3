CHECK = dict(name='mmtime-host', tier=1,
    covers=['userland/lib/mmtime/**', 'tools/tests/mmtime_host.c',
            'tools/tests/test_mmtime.py', 'tools/tests/host32.py',
            'tools/tests/cc_cache.py', 'sdk/include/os32/**', 'include/memmap.h',
            'include/abi.h', 'include/callconv.h', 'arch/x86/arch_abi.h'],
    needs=[], budget=4.0)


def run(ctx):
    import os
    import subprocess
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        argv = ['python3', '-B', 'tools/tests/test_mmtime.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root,
                             env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0
