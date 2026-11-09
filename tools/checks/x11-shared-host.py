#!/usr/bin/env python3
CHECK = dict(name='x11-shared-host', tier=1,
    covers=['kernel/ime/ime_display_private.h', 'platform/pc98/ime_default_render.h', 'platform/pc98/ime_display.c', 'include/ime_control.h', 'arch/x86/kernel/kapi_call.c', 'arch/x86/kernel/syscall_entry.c', 'tools/tests/syscall_source.py', 'include/ksetjmp.h', 'arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'platform/pc98/irq_policy.c', 'platform/pc98/platform_irq_limits.h', 'gfx/gfx_emergency.c', 'gfx/gfx_emergency.h', 'kernel/ime/ime_cui_process.inc', 'kernel/ime/ime_commit_take.inc', 'tools/tests/x11_shared_host_l45.inc', 'kernel/ime/ime_delivery.inc', 'tools/tests/x11_shared_host_imetab.inc', 'tools/tests/font_guard_inactive.h', 'kernel/ime/ime.c', 'kernel/ime/ime.h', 'kernel/ime/ime_romkana.c', 'kernel/ime/ime_render.h', 'kernel/gui.c', 'kernel/gui.h', 'kernel/console/con_sink.c', 'kernel/kbd_inject.c', 'include/con_sink.h', 'include/kbd_inject.h', 'drivers/input/kbd.c', 'drivers/input/kbd_fgpoll.inc', 'drivers/video/geforce/geforce_module.h', 'drivers/input/kbd.h', 'exec/exec.c', 'exec/appslot.c', 'exec/appslot.h', 'exec/exec_heap.c', 'exec/redir_access.c', 'kernel/irq/irq.c', 'kernel/irq/irq_math.c', 'mm/kmalloc.c', 'arch/x86/kernel/setjmp.asm', 'lib/kstring.c', 'lib/kstring_c.c', 'lib/utf8.c', 'tools/tests/test_x11_shared.py', 'tools/tests/x11_shared_host.c', 'tools/tests/x11_park_host.c', 'tools/tests/test_exec_r1.py', 'tools/tests/host32.py', 'tools/tests/host_arch/arch_io.h', 'kernel/sqlite_engine.h', 'arch/x86/kernel/cpu_flags.inc', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'platform/pc98/d2_bind.c', 'drivers/input/kbd_dlog.c', 'drivers/input/kbd_dlog.h', 'drivers/input/mouse.h', 'tools/tests/pointer_fixture_host.h', 'include/clock_service.h', 'kernel/time/clock_service.c', 'drivers/char/rtc.h', 'tools/tests/port_fixture_host.h', 'kernel/port_service.c', 'kernel/serial_diag.c', 'include/port_service.h', 'include/console_output.h', 'drivers/char/serial.h', 'lib/serial_diag.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'kdebug/selftest_kbd_inject.c', 'tools/tests/kbd_inject_host.c', 'tools/tests/test_kbd_inject.py', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'include/kapi_call.h', 'include/kapi_call_generated.h', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h'],
    needs=[], budget=4.7)


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
