#!/usr/bin/env python3
"""M6[3]: execute resource generation/reservation and exec boundary adapters."""
CHECK = dict(name='input-m6-3-host', tier=1, budget=8, needs=['make:all'],
    basis=['docs/decisions/INPUT_BIND_DESIGN.md'],
    covers=['input/hal/input_context.c', 'input/hal/input_interrupt.c',
            'input/hal/input_context.h', 'exec/appslot.c', 'exec/appslot.h',
            'exec/appslot_selftest.inc', 'exec/exec_abort.inc', 'exec/shell_service.h',
            'tools/tests/input_m6_3_host.c', 'tools/tests/test_input_m6_3.py',
            'tools/tests/hwb_fake.h', 'tools/tests/host_arch/**', 'userland/shell/hwb.h',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'include/**',
            'mm/pgalloc.h', 'exec/exec_surface.h', 'fs/fd_redirect.h',
            'gfx/gfx_core_init.inc', 'kdebug/gf_display_test.c',
            'tools/tests/gf4_a3_host.c', 'tools/tests/gf4_display_fake.h',
            'tools/tests/gf_accel_unbound_stubs.c',
            'tools/tests/gf_command_runtime_host.h', 'hwdev/gf/**',
            'drivers/video/geforce/**', 'kdebug/gf_display_test.h',
            'kdebug/gf_command_wire.h', 'sdk/include/os32/**'])
from pathlib import Path
from types import SimpleNamespace
import subprocess, sys, tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('owner-only', 'appslot', 'gfx_resource_gen != resource_gen', '0'),
    ('no-issue', 'appslot', 'if (++next_resource_gen == 0) ++next_resource_gen;', 'if (!next_resource_gen) ++next_resource_gen;'),
    ('zero-wrap', 'appslot', 'if (++next_resource_gen == 0) ++next_resource_gen;', '++next_resource_gen;'),
    ('no-exit-discard', 'interrupt', '        input_interrupt_discard();', '        (void)0;'),
    ('no-reservation', 'appslot', '    gfx_resource_reserve(owner);', '    (void)owner;'),
    ('no-boundary', 'adapter', '        input_interrupt_exec_exit((u32)id, gen);', '        (void)gen;'),
]
def run(ctx):
    sources = dict(appslot=(ctx.root/'exec/appslot.c').read_text(),
                   context=(ctx.root/'input/hal/input_context.c').read_text(),
                   interrupt=(ctx.root/'input/hal/input_interrupt.c').read_text())
    abort = (ctx.root/'exec/exec_abort.inc').read_text()
    start = abort.index('static void exec_input_owner_exit(int id)')
    take_start = abort.index('u32 exec_hwb_abort_take(void)')
    take_end = abort.index('\n}', take_start) + 2
    sources['adapter'] = abort[start:abort.index('\nu32 exec_hwb_abort_take', start)] + abort[take_start:take_end]
    with tempfile.TemporaryDirectory(prefix='input-m6-3-') as td:
        tmp = Path(td)
        prefix = (ctx.root/'gfx/gfx_core_init.inc').read_text().split('    palette_shadow_reset();', 1)[0]
        (tmp/'m6_gfx_acquire.inc').write_text(prefix.split('{\n', 1)[1])
        def execute(edits):
            for key, code in edits.items():
                (tmp/('appslot_copy.c' if key == 'appslot' else 'exec_adapter.inc' if key == 'adapter' else key+'.c')).write_text(code)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-Wno-address', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static', '-no-pie',
                '-Wl,--gc-sections', '-I'+str(tmp), '-I'+str(ctx.root/'tools/tests/host_arch'),
                '-I'+str(ctx.root/'tools/tests'), '-I'+str(ctx.root/'input/hal'),
                *include_flags(ctx.root), str(ctx.root/'tools/tests/input_m6_3_host.c'),
                '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu', timeout=10).returncode
        assert execute(sources) == 0
        if ctx.mutate:
            for name, key, old, new in MUTATIONS:
                assert sources[key].count(old) == 1, name
                edited = dict(sources); edited[key] = sources[key].replace(old, new, 1)
                assert execute(edited) == 1, name
                print('RED runtime: '+name, flush=True)
            assert execute(sources) == 0
    # Diagnostic enter/leave with real M6 state and the shipped gfx acquisition prologue.
    with tempfile.TemporaryDirectory(prefix='input-m6-display-') as td:
        tmp = Path(td)
        prefix = (ctx.root/'gfx/gfx_core_init.inc').read_text().split('    palette_shadow_reset();', 1)[0]
        (tmp/'m6_gfx_acquire.inc').write_text(prefix.split('{\n', 1)[1])
        exe = tmp/'display'
        command = ['gcc', '-DINPUT_M6_3_REAL', '-I'+str(ctx.root/'tools/tests/host_arch'),
            '-I'+str(tmp), '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
            '-Wextra', '-Werror', '-Wno-address', '-Wno-unused-parameter', '-ffreestanding',
            '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-D__KERNEL_BUILD__',
            *include_flags(ctx.root), '-I'+str(ctx.root), '-I'+str(ctx.root/'build/out/geforce'),
            str(ctx.root/'tools/tests/gf4_a3_host.c'),
            *[str(ctx.root/x) for x in ['drivers/video/geforce/edid.c', 'tools/tests/gf_accel_unbound_stubs.c']], '-o', str(exe)]
        subprocess.run(command, check=True)
        assert host32.run([str(exe)], runner='qemu', timeout=30).returncode == 0
        print('PASS M6[3] (6) diagnostic leave/enter, IRQ and ESTATE with no pending', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
