"""PowerMac cooperative terminal output host acceptance."""
CHECK = dict(name='ppc-stream-host', tier=1, needs=[], budget=15,
    covers=['tools/tests/test_ppc_stream.py', 'tools/tests/ppc_stream_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'platform/powermac/console_stream.c', 'platform/powermac/console_log.c',
            'platform/powermac/lan_terminal.c', 'platform/powermac/lan_boot.h',
            'platform/powermac/ofw.h', 'platform/powermac/kboot_log.h',
            'arch/ppc/kernel_boot.h', 'kernel/console/console_stream.h',
            'sdk/gen_kapi.py', 'sdk/kapi.json', 'sdk/kapi_abi_x86.py',
            'sdk/kapi_target_ppc.py', 'sdk/target_profile.py', 'arch/ppc/ppc_boot.h',
            'build/*.mk', 'build/**/*.mk', 'sdk/include/**', 'gfx/*.h',
            'kernel/console/*.h', 'include/**', 'lib/kstring.h', 'lib/kstring_c.c'])
def run(ctx):
    import subprocess
    return subprocess.call(['python3','-B','tools/tests/test_ppc_stream.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
