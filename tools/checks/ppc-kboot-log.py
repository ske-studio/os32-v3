"""Host acceptance for the PowerMac LOG channel and ESCC receiver."""

CHECK = dict(name='ppc-kboot-log', tier=1, budget=10, needs=[],
    basis=['docs/ppc/README.md ESCC の受信'],
    covers=['platform/powermac/log.c', 'platform/powermac/kboot_log.h',
            'platform/powermac/escc.c', 'platform/powermac/ofw.h',
            'platform/powermac/platform_of_layout.h', 'arch/ppc/ppc_boot.h',
            'include/types.h', 'include/callconv.h', 'tools/ppc/kboot_log_host.c',
            'kernel/port_service.c', 'include/port_service.h',
            'include/console_output.h', 'sdk/include/os32/os32_kapi_shared.h',
            'tools/ppc/kboot_log.py'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/ppc/kboot_log.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
