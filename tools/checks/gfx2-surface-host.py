#!/usr/bin/env python3
CHECK = dict(name='gfx2-surface-host', tier=1, needs=[], budget=10,
    covers=['gfx/v2/surface.c', 'gfx/v2/gfx2_int.h', 'include/gfx_hal.h',
            'include/gfx_hal2.h', 'include/gfx_sync.h', 'include/types.h',
            'include/type_contract.h', 'include/callconv.h', 'include/abi.h',
            'arch/x86/arch_abi.h', 'include/memmap.h',
            'gfx/gfx.h', 'include/pc98.h', 'exec/surface_limits.h',
            'include/gfx_mode.h', 'include/gfx_format.h', 'include/sys.h',
            'mm/paging.h', 'mm/physmem.h', 'include/appmem_types.h',
            'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_host_notice.h',
            'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_surface.h',
            'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_kapi_generated.h',
            'mm/pgalloc.h', 'lib/kstring.h', 'sdk/include/os32/os32_gfx2.h',
            'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/host32.py',
            'tools/tests/gfx2_surface_host.c', 'tools/tests/test_gfx2_surface.py'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_surface.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
