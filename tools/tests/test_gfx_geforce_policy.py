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
    'sys/dev/video/geforce/backend_geforce.c', 'sys/dev/video/geforce/gf_direct.c',
    'sys/dev/video/geforce/gf_hook.c', 'sys/dev/video/geforce/gf_v2.h', 'gfx/v2/*.h', 'sdk/include/os32/os32_gfx2.h',
    'sys/dev/video/geforce/geforce_module.h', 'tools/tests/gfx_geforce_policy_host.c',
    'tools/tests/gf_accel_unbound_stubs.c', 'tools/tests/test_gfx_geforce_policy.py',
    'sys/platform/pc98/video/pc98_route.h', 'hwdev/gf/**', 'sys/dev/video/geforce/gfinit_mode.h', 'sys/hal/gfx/palette.h',
    'sys/dev/video/geforce/leave_log.h', 'include/**',
    'sys/hal/gfx/gfx_policy.c', 'sys/hal/gfx/gfx_policy.h', 'sys/hal/gfx/gfx_internal.h', 'sys/hal/gfx/gfx.h', 'sys/kern/gfx/osri/gfx_osri_view.h',
    'sys/include/gfx_hal.h', 'sys/include/con_sink.h', 'sys/kern/mm/pgalloc.h', 'sys/kern/exec/appslot.h',
    'sys/arch/x86/arch_exec_storage.h',
    'sys/arch/x86/arch_io.h',
    'sys/arch/x86/arch_jmp_storage.h',
    'sys/arch/x86/kernel/x87.h',
    'sys/arch/x86/x86_flags.h',
    'sys/dev/pci/pci_decode.h',
    'sys/kern/exec/surface_limits.h',
    'sys/fs/vfs_stdin.h',
    'sys/dev/video/geforce/gf_load_state.h',
    'sys/dev/video/geforce/diag/gfinit_decode.h',
    'sys/dev/video/geforce/diag/gfinit_pll.h',
    'sys/dev/video/geforce/diag/gfinit_wire.h',
    'sys/dev/video/geforce/diag/recover.h',
    'sys/dev/video/geforce/diag/recover_gf.h',
    'sys/kern/module/module_image.h',
    'sys/kern/module/module_runtime.h',
    'sys/kern/module/module_schema.h',
    'sys/kern/mm/paging.h',
    'sys/kern/mm/physmem.h',
    'sys/platform/pc98/platform_io.h',
    'sdk/include/os32/os32_generations.h',
    'sdk/include/os32/os32_host_notice.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_ls.h',
    'sdk/include/os32/os32_surface.h',
    'sdk/include/os32/os32_ver_info.h',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'],
    basis=['GFX_ACCEL_HAL_DESIGN §2', 'GFX_ACCEL_HAL_DESIGN §3-1', 'GFX_ACCEL_HAL_DESIGN §3-4'],
)
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('v2-lost', ' && !gf_v2_inst.lost;', ';'),
    ('v2-scan-guard', '    if (gf_v2_owns(front)) return;', '    (void)front;'),
    ('scan-notify', '    gfx_policy_scan_changed();', '    (void)0;'),
    ('stop-reeval', '    gfx_policy_reeval();\n    (void)gf_v2_idle();\n#endif\n    int rc = stop_device', '    (void)0;\n    (void)gf_v2_idle();\n#endif\n    int rc = stop_device'),
    ('enter-reeval', '    gfx_policy_reeval();\n#endif\n}\nstatic void geforce_leave', '    (void)0;\n#endif\n}\nstatic void geforce_leave'),
    ('direct-failure', '            geforce_fail(); /* DIRECT device failure */', '            (void)0; /* DIRECT device failure */'),
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
        path = 'sys/dev/video/geforce/backend_geforce.c'
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
