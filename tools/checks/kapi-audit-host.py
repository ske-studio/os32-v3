#!/usr/bin/env python3
CHECK = dict(name='kapi-audit-host', tier=1,
    covers=['drivers/video/pc98/pc98_route.h', 'tools/tests/font_guard_inactive.h', 'kernel/ime/ime.c', 'drivers/input/kbd.c', 'kernel/ime/ime_romkana.c', 'lib/kstring.c', 'lib/kstring_c.c', 'lib/utf8.c', 'kernel/console/console.c', 'drivers/sound/fm/fm.c', 'drivers/sound/fm/fm.h', 'drivers/char/serial.c', 'sdk/kapi.json', 'sdk/gen_kapi.py', 'kapi/kapi_generated.c', 'tools/tests/kapi_audit_host.c', 'tools/tests/test_kapi_audit.py', 'tools/tests/host32.py', 'tools/check_map.d/check-kapi-audit-host.yaml', 'build/checks.d/check-kapi-audit-host.mk', 'exec/exec.c', 'exec/exec_image.inc', 'exec/exec.h', 'exec/appslot.h', 'drivers/input/kbd.h', 'drivers/char/serial.h', 'kernel/ime/ime.h', 'kernel/ime/ime_dict.c', 'tools/tests/test_exec_r1.py', 'exec/exec_heap.c', 'exec/redir_access.c', 'kernel/irq/irq.c', 'kernel/irq/irq_math.c', 'mm/kmalloc.c', 'arch/x86/kernel/setjmp.asm', 'exec/exec_kapi_init.inc', 'include/con_sink.h', 'fs/fd_redirect.h', 'kernel/sqlite_engine.h', 'arch/x86/kernel/cpu_flags.inc', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'platform/pc98/d2_bind.c', 'drivers/input/kbd_dlog.c', 'drivers/input/kbd_dlog.h', 'drivers/input/mouse.h', 'tools/tests/pointer_fixture_host.h', 'include/port_service.h', 'include/console_output.h', 'kernel/port_service.c', 'kernel/serial_diag.c', 'lib/serial_diag.h', 'drivers/char/serial_plan.c', 'drivers/char/serial_plan.h', 'include/clock_service.h', 'kernel/time/clock_service.c', 'drivers/char/rtc.h', 'kernel/console/console_backend.h', 'platform/pc98/console.c', 'include/console_attr.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3.07983)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kapi_audit.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
