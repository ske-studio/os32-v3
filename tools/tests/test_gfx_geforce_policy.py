"""G3: GeForce scan publication, policy, capabilities and DIRECT present."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

CHECK = dict(name='gfx-geforce-policy-host', tier=1, needs=[], budget=15, covers=[
    'drivers/video/geforce/backend_geforce.c', 'drivers/video/geforce/gf_direct.c',
    'drivers/video/geforce/geforce_module.h', 'tools/tests/gfx_geforce_policy_host.c',
    'tools/tests/gf_accel_unbound_stubs.c', 'tools/tests/test_gfx_geforce_policy.py',
    'drivers/video/pc98/pc98_route.h', 'hwdev/gf/**', 'hwdev/gfinit_mode.h', 'gfx/palette.h',
    'drivers/video/geforce/leave_log.h', 'include/**',
    'gfx/gfx_policy.c', 'gfx/gfx_policy.h', 'gfx/gfx_internal.h', 'gfx/gfx.h', 'gfx/gfx_osri_view.h',
    'include/gfx_hal.h', 'include/con_sink.h', 'mm/pgalloc.h', 'exec/appslot.h',
    'arch/x86/arch_exec_storage.h',
    'arch/x86/arch_io.h',
    'arch/x86/arch_jmp_storage.h',
    'arch/x86/kernel/x87.h',
    'arch/x86/x86_flags.h',
    'drivers/pci/pci_decode.h',
    'exec/surface_limits.h',
    'fs/vfs_stdin.h',
    'hwdev/gf_load_state.h',
    'kdebug/gfinit_decode.h',
    'kdebug/gfinit_pll.h',
    'kdebug/gfinit_wire.h',
    'kdebug/recover.h',
    'kdebug/recover_gf.h',
    'kernel/module/module_image.h',
    'kernel/module/module_runtime.h',
    'kernel/module/module_schema.h',
    'mm/paging.h',
    'mm/physmem.h',
    'platform/pc98/platform_io.h',
    'sdk/include/os32/os32_generations.h',
    'sdk/include/os32/os32_host_notice.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_ls.h',
    'sdk/include/os32/os32_surface.h',
    'sdk/include/os32/os32_ver_info.h',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('scan-notify', '    gfx_policy_scan_changed();', '    (void)0;'),
    ('started', 'active && gfx_is_started() && gf_direct_ok()', 'active && gf_direct_ok()'),
    ('active', 'active && gfx_is_started() && gf_direct_ok()', 'gfx_is_started() && gf_direct_ok()'),
    ('capability', 'info->flags |= GFX_CAP_DIRECT;', 'info->flags |= 0;'),
    ('generation', 'info->reserved[0] = gfx_policy_gen_get();', 'info->reserved[0] = 0;'),
    ('present', '        geforce_context->counters->commits++;\n        return;', '        return;'),
)

def run_case(folder, runner, mutation=None):
    edits = {}
    if mutation:
        name, old, new = mutation
        path = 'drivers/video/geforce/backend_geforce.c'
        source = (ROOT / path).read_text()
        assert source.count(old) == 1, name
        edits[path] = source.replace(old, new)
    exe = folder / 'g3-host'
    command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
               '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
               '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
               *include_flags(ROOT),
               '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
               '-D__KERNEL_BUILD__', '-I'+str(ROOT),
               str(ROOT / 'tools/tests/gfx_geforce_policy_host.c'),
               str(ROOT / 'tools/tests/gf_accel_unbound_stubs.c'), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), result
    if mutation:
        assert result.returncode == 1, result
    print(('MUTATE RED ' + mutation[0]) if mutation else 'PASS G3 policy/transitions (' + runner + ')')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        for mutation in (None, *MUTATIONS) if args.mutate else (None,):
            with tempfile.TemporaryDirectory(prefix='gfx-accel-hal-') as td:
                run_case(Path(td), runner, mutation)


def run(ctx) -> int:
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_gfx_geforce_policy.py', *(['--mutate'] if ctx.mutate else [])]
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
