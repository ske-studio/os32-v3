#!/usr/bin/env python3
CHECK = dict(name='ime-fg1-host', tier=1,
    covers=['sys/input/keymaps/hid_jp.c', 'sys/input/input_ime_gate.c', 'sys/input/input_context.h', 'sys/hal/input/legacy_keymap.c', 'sys/hal/input/keymap.h', 'sys/input/keymaps/pc98_jp.c', 'sys/hal/input/keymap_registry.c', 'sys/hal/input/keymap_eval.c', 'sys/hal/input/keymap_state.c', 'sys/platform/pc98/input/kbd_hal.c', 'sys/hal/input/in2_dev.c', 'sys/hal/input/in2_state.c', 'sys/hal/input/kbd_merge.c', 'tools/tests/kbd_capture_fixture_host.h', 'sys/hal/input/legacy_ring.c', 'sys/hal/input/legacy_mode.c', 'sys/kern/input_legacy.c', 'sys/kern/input_wait.c', 'sys/hal/input/in2_int.h', 'sys/kern/ime/ime_display_private.h', 'sys/platform/pc98/platform_ime_render.h', 'sys/platform/pc98/ime_display.c', 'sys/include/ime_control.h', 'sys/arch/x86/kernel/kapi_call.c', 'sys/arch/x86/kernel/syscall_entry.c', 'tools/tests/syscall_source.py', 'sys/include/ksetjmp.h', 'sys/arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/platform/pc98/irq_policy.c', 'sys/platform/pc98/platform_irq_limits.h', 'sys/hal/gfx/gfx_emergency.c', 'sys/hal/gfx/gfx_emergency.h', 'sys/kern/ime/ime_cui_process.inc', 'sys/kern/ime/ime_commit_take.inc', 'tools/tests/x11_shared_host_l45.inc', 'sys/kern/ime/ime_delivery.inc', 'tools/tests/x11_shared_host_imetab.inc', 'tools/tests/font_guard_inactive.h', 'sys/kern/ime/ime.c', 'sys/kern/ime/ime.h', 'sys/include/ime_boot.h', 'sys/kern/ime/ime_romkana.c', 'sys/kern/ime/ime_render.h', 'sys/kern/gui.c', 'sys/kern/gui.h', 'sys/kern/console/con_sink.c', 'sys/kern/kbd_inject.c', 'sys/include/con_sink.h', 'sys/include/kbd_inject.h', 'sys/platform/pc98/input/kbd.c', 'sys/platform/pc98/input/kbd_fgpoll.inc', 'sys/dev/video/geforce/geforce_module.h', 'sys/platform/pc98/input/kbd.h', 'sys/kern/exec/exec.c', 'sys/kern/exec/appslot.c', 'sys/kern/exec/appslot.h', 'sys/kern/exec/exec_heap.c', 'sys/kern/exec/redir_access.c', 'sys/kern/irq/irq.c', 'sys/kern/irq/irq_math.c', 'sys/kern/mm/kmalloc.c', 'sys/arch/x86/kernel/setjmp.asm', 'sys/kern/kstring.c', 'sys/kern/kstring_c.c', 'lib/utf8.c', 'tools/tests/test_x11_shared.py', 'tools/tests/x11_shared_host.c', 'tools/tests/x11_park_host.c', 'tools/tests/test_exec_r1.py', 'tools/tests/host32.py', 'tools/tests/host_arch/arch_io.h', 'sys/kern/sqlite_engine.h', 'sys/kern/sqlite_engine_body.h', 'sys/arch/x86/kernel/cpu_flags.inc', 'sys/hal/input/input_service.c', 'sys/hal/input/pointer_service.h', 'sys/hal/input/input_service.h', 'sys/include/kbd.h', 'sys/platform/pc98/d2_bind.c', 'sys/dev/input/kbd_dlog.c', 'sys/dev/input/kbd_dlog.h', 'sys/platform/pc98/input/mouse.h', 'tools/tests/pointer_fixture_host.h', 'sys/hal/time/clock_service.h', 'sys/hal/time/clock_service.c', 'sys/dev/char/rtc.h', 'tools/tests/port_fixture_host.h', 'sys/kern/port_service.c', 'sys/kern/serial_diag.c', 'sys/include/port_service.h', 'sys/include/console_output.h', 'sys/dev/char/serial.h', 'lib/serial_diag.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sys/kern/kdebug/selftest_kbd_inject.c', 'tools/tests/kbd_inject_host.c', 'tools/tests/test_kbd_inject.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/include/kapi_call.h', 'sys/include/kapi_call_generated.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
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
    for name, old, new, marker in [
        ('STOP', 'result->compat_sc != KEY_STOP && !', '1 && !', 'held USB STOP'),
        ('ETX', 'result->sym == 3', 'result->sym == 4', 'full queue capture'),
        ('unregistered', 'if (input_context_snapshot(&dst)) return 0;', 'if (input_context_snapshot(&dst)) return 1;', 'unregistered STOP'),
        ('repeat', '(*held_byte & bit) || ', '', 'full queue drained'),
    ]:
        yield (name, 'kbd_merge_source.inc', old, new, 'FAIL ' + marker)
    yield ('legacy RLDN as ETX', 'kbd_merge_source.inc', 'result->kind == KM_CHAR && result->sym == 3',
           '(result->kind == KM_CHAR && result->sym == 3) || (result->kind == KM_LEGACY_CONTROL && result->op == 3)',
           'FAIL RLDN stays legacy')
    yield ('table retains actions', 'legacy_keymap_source.inc', 'if (p && p->flush_actions) p->flush_actions();',
           '(void)p;', 'FAIL table flushes ON')
    yield ('cancel wake removed', 'wait_source.inc', 'if (cancel && input_ime_cancel_pending()) step = IR_RECHECK;',
           'if (0) step = IR_RECHECK;', 'FAIL cancel alone wakes waiter')
    yield ('toggle always', 'ime_source.inc', 'if (ime_is_active() != target) ime_toggle();',
           'ime_toggle();', 'FAIL ordered OFF ON')
    yield ('absolute success fixed', 'ime_source.inc', 'return ime_is_active() == target ? 0 : -1;',
           'return 0;', 'FAIL failed ON')
    from test_x11_shared import ROOT
    product = (ROOT / 'sys/kern/ime/ime.c').read_text()
    for name in ('ime_getchar_wait', 'ime_getkey_wait', 'ime_trygetchar', 'ime_trygetkey',
                 'ime_getchar_kapi', 'ime_getkey_kapi', 'ime_feed_key', 'ime_take_cancel',
                 'ime_publish_state', 'ime_cancel_input'):
        start = product.index(name + '(')
        if name == 'ime_cancel_input':
            start = product.index('ime_cancel_input(void)\n{')
        body = product[start:product.index('\n}', start) + 2]
        if name.endswith('_wait'):
            cases = [('resume = step == IR_RECHECK;', 'resume = 0;', 'FEP wait retains deadline'),
                     ('        ime_take_cancel();', '        (void)0;', 'pre-wait cancel'),
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
