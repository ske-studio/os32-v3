#!/usr/bin/env python3
CHECK = dict(name='x11-shared-host', tier=1,
    covers=['tools/tests/font_guard_inactive.h', 'kernel/ime/ime.c', 'kernel/ime/ime.h', 'kernel/ime/ime_romkana.c', 'kernel/ime/ime_render.h', 'kernel/gui.c', 'kernel/gui.h', 'kernel/console/con_sink.c', 'kernel/kbd_inject.c', 'include/con_sink.h', 'include/kbd_inject.h', 'drivers/input/kbd.c', 'drivers/input/kbd.h', 'exec/exec.c', 'exec/appslot.c', 'exec/appslot.h', 'exec/exec_heap.c', 'exec/redir_access.c', 'kernel/irq/irq.c', 'kernel/irq/irq_math.c', 'mm/kmalloc.c', 'arch/x86/kernel/setjmp.asm', 'lib/kstring.c', 'lib/kstring_c.c', 'lib/utf8.c', 'tools/tests/test_x11_shared.py', 'tools/tests/x11_shared_host.c', 'tools/tests/x11_park_host.c', 'tools/tests/test_exec_r1.py', 'tools/tests/host32.py', 'tools/tests/host_arch/arch_io.h', 'kernel/sqlite_engine.h', 'arch/x86/kernel/cpu_flags.inc', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'platform/pc98/d2_bind.c', 'drivers/input/kbd_dlog.c', 'drivers/input/kbd_dlog.h', 'drivers/input/mouse.h', 'tools/tests/pointer_fixture_host.h', 'include/clock_service.h', 'kernel/time/clock_service.c', 'drivers/char/rtc.h', 'tools/tests/port_fixture_host.h', 'kernel/port_service.c', 'kernel/serial_diag.c', 'include/port_service.h', 'include/console_output.h', 'drivers/char/serial.h', 'lib/serial_diag.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=2.90963)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_x11_shared.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
