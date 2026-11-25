#!/usr/bin/env python3
CHECK = dict(name='gfx2-display-host', tier=1, needs=[], budget=10,
    covers=['sys/platform/pc98/platform_display_fallback.h', 'sys/platform/powermac/platform_display_fallback.h',
            'sys/include/display_provider.h', 'sys/hal/gfx/v2/display.c', 'sys/hal/gfx/v2/gfx2_int.h', 'sys/include/gfx_hal.h',
            'sys/hal/gfx/gfx_hal2.h', 'sys/include/gfx_sync.h', 'sys/include/types.h',
            'sys/include/type_contract.h', 'sys/include/callconv.h', 'sys/include/abi.h',
            'sys/arch/x86/arch_abi.h', 'sys/include/memmap.h',
            'sys/hal/gfx/gfx.h', 'sys/platform/pc98/pc98.h', 'sys/kern/exec/surface_limits.h',
            'sys/include/gfx_mode.h', 'sys/include/gfx_format.h', 'sys/include/sys.h',
            'sys/kern/mm/paging.h', 'sys/kern/mm/physmem.h', 'sys/include/appmem_types.h',
            'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_host_notice.h',
            'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_surface.h',
            'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_kapi_generated.h',
            'sys/kern/mm/pgalloc.h', 'sys/kern/kstring.h', 'sdk/include/os32/os32_gfx2.h',
            'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/host32.py',
            'tools/tests/gfx2_display_host.c', 'tools/tests/test_gfx2_display.py'],
    basis=['HAL_V2_DESIGN §9-2', 'HAL_V2_DESIGN §12-1'],
)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_display.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
