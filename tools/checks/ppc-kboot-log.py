"""Host acceptance for the PowerMac LOG channel and ESCC receiver."""

CHECK = dict(name='ppc-kboot-log', tier=1, budget=10, needs=[],
    basis=['docs/ppc/README.md ESCC の受信'],
    covers=['sys/platform/powermac/log.c', 'sys/platform/powermac/kboot_log.h',
            'sys/platform/powermac/escc.c', 'sys/platform/powermac/ofw.h',
            'sys/platform/powermac/platform_of_layout.h', 'sys/arch/ppc/ppc_boot.h',
            'sys/include/types.h', 'sys/include/callconv.h', 'tools/ppc/kboot_log_host.c',
            'sys/kern/port_service.c', 'sys/include/port_service.h',
            'sys/include/console_output.h', 'sdk/include/os32/os32_kapi_shared.h',
            'tools/ppc/kboot_log.py'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/ppc/kboot_log.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
