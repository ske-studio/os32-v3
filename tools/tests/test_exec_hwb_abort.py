#!/usr/bin/env python3
"""HW_BASIC_DESIGN §3/F: actual exec.c/AppSlot, libc-free ILP32 controls.

Cover CUI shell state preservation, coalescing/consumption, IF=0/1 and a new
IRQ at restore; CUI app and GUI cooperative/wait/runaway controls stay live.
"""
CHECK = dict(name='exec-hwb-abort-host', tier=1,
    covers=['exec/ring3_str.c', 'exec/exec.c', 'exec/exec_*.inc', 'exec/exec_hwb.h',
            'exec/appslot.c', 'exec/appslot*.inc', 'exec/appslot.h',
            'tools/tests/exec_hwb_abort_host.c', 'tools/tests/hwb_fake.h',
            'tools/tests/hwb_host.py', 'tools/tests/host32.py',
            'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py',
            'tools/tests/host_arch/**', 'include/**', 'exec/*.h',
            'arch/x86/**', 'platform/pc98/**', 'sdk/include/os32/**',
            'userland/shell/hwb.h', 'kdebug/hwb_wire.h', 'build/config.mk',
            'kernel/console/font_guard.h', 'kernel/console/font_source.h',
            'arch/x86/legacy/v86_mem.h', 'arch/x86/legacy/v86.h', 'kernel/kselftest.h',
            'kernel/ime/ime.h', 'kernel/ime/ime_render.h', 'kernel/gui.h',
            'mm/paging.h', 'mm/physmem.h', 'mm/kmalloc.h', 'mm/pgalloc.h',
            'mm/shlib.h', 'mm/shm.h', 'lib/serial_diag.h', 'lib/kstring.h',
            'lib/path.h', 'fs/vfs.h', 'fs/vfs_stdin.h',
            'fs/fd_redirect.h', 'fs/pipe_buffer.h',
            'gfx/gfx.h', 'drivers/sound/fm/snd_engine.h',
            'kapi/kapi_host.h', 'kapi/kapi_db.h', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h'],
    needs=[], budget=5.8)

import sys
import hwb_host

FIXTURE = 'tools/tests/exec_hwb_abort_host.c'
FLAGS = ('-Itools/tests/host_arch',
         '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
         '-D__KERNEL_BUILD__', '-Wno-unused-but-set-variable')
MUTATIONS = [
    ('display-stop-record', 'exec/exec_abort.inc',
     '        g_cui_display_abort_pending = 1;',
     '        g_cui_display_abort_pending = 0;', 'display-return'),
    ('display-stop-owner', 'exec/exec_abort.inc',
     'if (pending) appslot_gfx_owner_exit(APP_ID_SHELL);',
     'if (pending) appslot_gfx_owner_exit(APP_ID_SHELL + 1);', 'display-return'),

    ('shell-flag-disabled', 'exec/exec_abort.inc',
     '        g_hwb_abort_pending = 1;', '        g_hwb_abort_pending = 0;', 'take-clear'),
    ('take-not-cleared', 'exec/exec_abort.inc',
     '    g_hwb_abort_pending = 0;', '    /* retain pending request */', 'take-clear'),
    ('no-irq-save', 'exec/exec_abort.inc',
     '    unsigned int flags = irq_save();', '    unsigned int flags = 0;', 'cui-shell'),
    ('no-irq-restore', 'exec/exec_abort.inc',
     '    irq_restore(flags);', '    (void)flags;', 'cui-shell'),
    ('cui-app-diverted', 'exec/exec_abort.inc',
     '!con_sink_is_enabled() && appslot_cur() == APP_ID_SHELL',
     '!con_sink_is_enabled()', 'legacy-routes'),
    ('gui-shell-diverted', 'exec/exec_abort.inc',
     '!con_sink_is_enabled() && appslot_cur() == APP_ID_SHELL',
     'appslot_cur() == APP_ID_SHELL', 'gui-shell'),
]

if __name__ == '__main__':
    hwb_host.verify(FIXTURE, flags=FLAGS)
    if '--mutate' in sys.argv:
        raise SystemExit(hwb_host.mutations(FIXTURE, MUTATIONS, flags=FLAGS))


def run(ctx) -> int:
    import os
    import subprocess
    argv = ['python3', '-B', 'tools/tests/test_exec_hwb_abort.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
