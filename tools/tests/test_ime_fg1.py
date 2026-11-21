#!/usr/bin/env python3
CHECK = dict(name='ime-fg1-host', tier=1,
    covers=['input/hal/input_ime_gate.c', 'input/hal/input_context.h', 'input/hal/legacy_keymap.c', 'input/hal/keymap.h', 'input/keymaps/pc98_jp.c', 'input/hal/keymap_registry.c', 'input/hal/keymap_eval.c', 'input/hal/keymap_state.c', 'drivers/input/kbd_hal.c', 'input/hal/in2_dev.c', 'input/hal/in2_state.c', 'input/hal/kbd_merge.c', 'tools/tests/kbd_capture_fixture_host.h', 'input/hal/legacy_ring.c', 'input/hal/legacy_mode.c', 'kernel/input_legacy.c', 'kernel/input_wait.c', 'input/hal/in2_int.h', 'kernel/ime/ime_display_private.h', 'platform/pc98/platform_ime_render.h', 'platform/pc98/ime_display.c', 'include/ime_control.h', 'arch/x86/kernel/kapi_call.c', 'arch/x86/kernel/syscall_entry.c', 'tools/tests/syscall_source.py', 'include/ksetjmp.h', 'arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'platform/pc98/irq_policy.c', 'platform/pc98/platform_irq_limits.h', 'gfx/gfx_emergency.c', 'gfx/gfx_emergency.h', 'kernel/ime/ime_cui_process.inc', 'kernel/ime/ime_commit_take.inc', 'tools/tests/x11_shared_host_l45.inc', 'kernel/ime/ime_delivery.inc', 'tools/tests/x11_shared_host_imetab.inc', 'tools/tests/font_guard_inactive.h', 'kernel/ime/ime.c', 'kernel/ime/ime.h', 'include/ime_boot.h', 'kernel/ime/ime_romkana.c', 'kernel/ime/ime_render.h', 'kernel/gui.c', 'kernel/gui.h', 'kernel/console/con_sink.c', 'kernel/kbd_inject.c', 'include/con_sink.h', 'include/kbd_inject.h', 'drivers/input/kbd.c', 'drivers/input/kbd_fgpoll.inc', 'drivers/video/geforce/geforce_module.h', 'drivers/input/kbd.h', 'exec/exec.c', 'exec/appslot.c', 'exec/appslot.h', 'exec/exec_heap.c', 'exec/redir_access.c', 'kernel/irq/irq.c', 'kernel/irq/irq_math.c', 'mm/kmalloc.c', 'arch/x86/kernel/setjmp.asm', 'lib/kstring.c', 'lib/kstring_c.c', 'lib/utf8.c', 'tools/tests/test_x11_shared.py', 'tools/tests/x11_shared_host.c', 'tools/tests/x11_park_host.c', 'tools/tests/test_exec_r1.py', 'tools/tests/host32.py', 'tools/tests/host_arch/arch_io.h', 'kernel/sqlite_engine.h', 'kernel/sqlite_engine_body.h', 'arch/x86/kernel/cpu_flags.inc', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'platform/pc98/d2_bind.c', 'drivers/input/kbd_dlog.c', 'drivers/input/kbd_dlog.h', 'drivers/input/mouse.h', 'tools/tests/pointer_fixture_host.h', 'include/clock_service.h', 'kernel/time/clock_service.c', 'drivers/char/rtc.h', 'tools/tests/port_fixture_host.h', 'kernel/port_service.c', 'kernel/serial_diag.c', 'include/port_service.h', 'include/console_output.h', 'drivers/char/serial.h', 'lib/serial_diag.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'kdebug/selftest_kbd_inject.c', 'tools/tests/kbd_inject_host.c', 'tools/tests/test_kbd_inject.py', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'include/kapi_call.h', 'include/kapi_call_generated.h', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h'],
    needs=[], budget=4.7,
    basis=['docs/decisions/INPUT_BIND_DESIGN.md M7[3]'],
)


def run(ctx):
    from test_x11_shared import run as exercise
    exercise(fg1=True)
    if ctx.mutate:
        for mutation in mutations():
            exercise(mutation, fg1=True)
    return 0


def mutations():
    yield ('full cooked queue drops cancellation', 'ime_gate_source.inc',
           'if (!destination || !destination->active', 'if (legacy_cooked_pending() == KBD_BUF_SIZE || !destination || !destination->active',
           'FAIL full queue accepts cancel')
    from test_x11_shared import ROOT
    product = (ROOT / 'kernel/ime/ime.c').read_text()
    for name in ('ime_getchar_wait', 'ime_getkey_wait', 'ime_trygetchar', 'ime_trygetkey',
                 'ime_getchar_kapi', 'ime_getkey_kapi', 'ime_feed_key', 'ime_take_cancel',
                 'ime_publish_state', 'ime_cancel_input'):
        start = product.index(name + '(')
        if name == 'ime_cancel_input':
            start = product.index('ime_cancel_input(void)\n{')
        body = product[start:product.index('\n}', start) + 2]
        if name.endswith('_wait'):
            cases = [('        ime_take_cancel();', '        (void)0;', 'pre-wait cancel'),
                     ('int taken = ime_take_cancel();', 'int taken = 0;', 'returned key')]
        elif name.startswith('ime_try'):
            cases = [('ime_take_cancel();', '(void)0;', 'poll cancels')]
        elif name.endswith('_kapi'):
            cases = [('!cancelled && ', '', 'negative wait single cancel')]
        elif name == 'ime_feed_key':
            cases = [('int cancelled = ime_take_cancel();', 'int cancelled = 0;', 'GUI consumes pending'),
                     ('if (!cancelled)', 'if ((void)cancelled, 1)', 'GUI STOP once')]
        elif name == 'ime_take_cancel':
            cases = [('!_irq_enabled() || ', '', 'disabled poll preserves pending')]
        elif name == 'ime_publish_state':
            cases = [('if (!services_public) return;', '', 'private init has no destination'),
                     ('g_ime.rk.preedit[0] || ', '', 'romaji active'),
                     ('g_ime.mode != IME_MODE_OFF &&', 'g_ime.mode != IME_MODE_OFF ||', 'empty ON inactive')]
        else:
            cases = [('g_ime.commit_len = g_ime.commit_pos = 0;', '', 'pre-wait cancel')]
        for old, new, marker in cases:
            assert body.count(old) == 1
            yield (name + ' ' + marker, 'ime_source.inc', body, body.replace(old, new, 1),
                   'FAIL ' + marker)
