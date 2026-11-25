#!/usr/bin/env python3
"""HW_BASIC_DESIGN §3/F: actual exec.c/AppSlot, libc-free ILP32 controls.

Cover CUI shell state preservation, coalescing/consumption, IF=0/1 and a new
IRQ at restore; CUI app and GUI cooperative/wait/runaway controls stay live.
"""
CHECK = dict(name='exec-hwb-abort-host', tier=1,
    covers=['sys/input/input_context.c', 'sys/input/input_interrupt.c', 'sys/input/input_context.h', 'sys/kern/exec/ring3_str.c', 'sys/kern/exec/exec.c', 'exec/exec_*.inc', 'sys/kern/exec/exec_hwb.h',
            'sys/kern/exec/appslot.c', 'exec/appslot*.inc', 'sys/kern/exec/appslot.h',
            'tools/tests/exec_hwb_abort_host.c', 'tools/tests/hwb_fake.h',
            'tools/tests/hwb_host.py', 'tools/tests/host32.py',
            'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py',
            'tools/tests/host_arch/**', 'include/**', 'exec/*.h',
            'arch/x86/**', 'platform/pc98/**', 'sdk/include/os32/**',
            'userland/shell/hwb.h', 'sys/kern/kdebug/hwb_wire.h', 'build/config.mk',
            'sys/kern/console/font_guard.h', 'sys/kern/console/font_source.h',
            'sys/platform/pc98/legacy/v86_mem.h', 'sys/platform/pc98/legacy/v86.h', 'sys/kern/kselftest.h',
            'sys/kern/ime/ime.h', 'sys/kern/ime/ime_render.h', 'sys/kern/gui.h',
            'sys/kern/mm/paging.h', 'sys/kern/mm/physmem.h', 'sys/kern/mm/kmalloc.h', 'sys/kern/mm/pgalloc.h',
            'sys/kern/mm/shlib.h', 'sys/kern/mm/shm.h', 'lib/serial_diag.h', 'sys/kern/kstring.h',
            'lib/path.h', 'sys/fs/vfs.h', 'sys/fs/vfs_stdin.h',
            'sys/fs/fd_redirect.h', 'sys/fs/pipe_buffer.h',
            'sys/hal/gfx/gfx.h',
            'sys/kapi/kapi_host.h', 'sys/kapi/kapi_db.h', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=[], budget=5.8,
    basis=['HW_BASIC_DESIGN §3'],
)

import sys
import hwb_host

FIXTURE = 'tools/tests/exec_hwb_abort_host.c'
FLAGS = ('-Itools/tests/host_arch',
         '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
         '-D__KERNEL_BUILD__', '-Wno-unused-but-set-variable')
MUTATIONS = [
    ('display-stop-record', 'sys/kern/exec/exec_abort.inc',
     '        (void)input_display_restore_capture(APP_ID_SHELL);',
     '        (void)input_display_restore_capture(0);', 'display-return'),
    ('display-stop-owner', 'sys/kern/exec/exec_abort.inc',
     '(void)input_display_restore_drain();',
     '(void)0;', 'display-return'),

    ('shell-flag-disabled', 'sys/kern/exec/exec_abort.inc',
     '        g_hwb_abort_pending = 1;', '        g_hwb_abort_pending = 0;', 'take-clear'),
    ('take-not-cleared', 'sys/kern/exec/exec_abort.inc',
     '    u32 pending = g_hwb_abort_pending;\n    g_hwb_abort_pending = 0;', '    u32 pending = g_hwb_abort_pending;', 'take-clear'),
    ('no-irq-save', 'sys/kern/exec/exec_abort.inc',
     '    unsigned int flags = irq_save();', '    unsigned int flags = 0;', 'cui-shell'),
    ('no-irq-restore', 'sys/kern/exec/exec_abort.inc',
     '    irq_restore(flags);', '    (void)flags;', 'cui-shell'),
    ('cui-app-diverted', 'sys/kern/exec/exec_abort.inc',
     '!con_sink_is_enabled() && appslot_cur() == APP_ID_SHELL',
     '!con_sink_is_enabled()', 'legacy-routes'),
    ('gui-shell-diverted', 'sys/kern/exec/exec_abort.inc',
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
