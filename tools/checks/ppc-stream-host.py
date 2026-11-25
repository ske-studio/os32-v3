"""PowerMac cooperative terminal output host acceptance."""
CHECK = dict(name='ppc-stream-host', tier=1, needs=[], budget=15,
    covers=['sys/kern/console/console.c', 'tools/tests/test_ppc_stream.py', 'tools/tests/ppc_stream_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'sys/platform/powermac/console_stream.c', 'sys/platform/powermac/log_printf.c',
            'sys/platform/powermac/lan_terminal.c', 'sys/platform/powermac/lan_boot.h',
            'sys/platform/powermac/ofw.h', 'sys/platform/powermac/kboot_log.h',
            'sys/arch/ppc/kernel_boot.h', 'sys/kern/console/console_stream.h',
            'sdk/gen_kapi.py', 'sdk/kapi.json', 'sdk/kapi_abi_x86.py',
            'sdk/kapi_target_ppc.py', 'sdk/target_profile.py', 'sys/arch/ppc/ppc_boot.h',
            'build/*.mk', 'build/**/*.mk', 'sdk/include/**', 'gfx/*.h',
            'kernel/console/*.h', 'include/**', 'sys/kern/kprintf.c', 'sys/kern/kprintf_format.inc', 'sys/platform/powermac/platform_kprintf.h',
            'sys/kern/kstring.h', 'sys/kern/kstring_c.c'],
    basis=['PPC_FBCON_DESIGN §2', 'PPC_FBCON_DESIGN §6', 'AC-HOST-1'],
)
def run(ctx):
    import subprocess
    return subprocess.call(['python3','-B','tools/tests/test_ppc_stream.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
