"""OSRI KAPI owner, backend and rational run controller host tests."""
CHECK = dict(name='osri-view-host', tier=1,
    covers=['include/ksetjmp.h', 'arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'include/callconv.h', 'include/abi.h', 'arch/x86/arch_abi.h', 'kapi/kapi_osri.c', 'gfx/gfx_osri_view.c', 'gfx/gfx_osri_view.h',
            'gfx/gfx_osri.c', 'gfx/gfx_osri.h', 'gfx/gfx_core.c', 'gfx/gfx_internal.h',
            'gfx/gfx.h', 'gfx/palette.h', 'gfx/osri_profile.h',
            'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pc98_osri.c', 'drivers/video/pc98/splash_raster_run.c',
            'drivers/video/pc98/splash_raster_run.h', 'drivers/video/pc98/splash_raster_play.c',
            'drivers/video/pc98/splash_raster_image.h',
            'drivers/video/pc98/splash_raster_image.c', 'platform/pc98/splash.c',
            'platform/pc98/splash_image.c', 'init/boot_splash.c',
            'drivers/video/pc98/splash_raster_play.h', 'drivers/video/pc98/splash_raster_plan.c',
            'drivers/video/pc98/splash_raster.c', 'drivers/video/pc98/splash_raster.h',
            'tools/tests/test_osri_view.py', 'tools/tests/osri_view_host.c',
            'tools/tests/test_splash_wire.py', 'tools/tests/splash_raster_host.c',
            'tools/tests/splash_wire_host.c',
            'tools/tests/kernel_includes.py', 'tools/tests/host32.py',
            'include/gfx_hal.h', 'include/gfx_mode.h', 'include/gfx_format.h',
            'include/config.h', 'include/types.h', 'include/pc98.h', 'include/pegc.h',
            'include/memmap.h', 'include/io.h', 'include/con_sink.h', 'include/redir_access.h',
            'include/input_service.h', 'include/kbd.h', 'include/pcm_contract.h',
            'exec/appslot.h', 'exec/exec.h', 'fs/vfs.h', 'fs/serialfs/serialfs.h',
            'fs/serialfs/sfs_client.h', 'mm/kmalloc.h', 'lib/kstring.h', 'lib/serial_diag.h',
            'lib/le_internal.h', 'lib/crc32_core.inc', 'sdk/include/os32/os32_kapi_shared.h'],
    needs=[], budget=5.6)
import subprocess

def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_osri_view.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
