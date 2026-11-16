#!/usr/bin/env python3
"""M7[3]: real IME + context + cancellation gate through CUI and GUI readers."""
CHECK = dict(name='input-m7-3-host', tier=1, budget=12, needs=[],
    basis=['docs/decisions/INPUT_BIND_DESIGN.md'],
    covers=['arch/x86/kernel/exec_context.c', 'arch/x86/kernel/kapi_call.c',
            'arch/x86/kernel/setjmp.asm', 'arch/x86/kernel/syscall_entry.c',
            'gfx/gfx_emergency.c', 'input/keymaps/pc98_jp.c',
            'kdebug/selftest_kbd_inject.c', 'kernel/irq/irq.c', 'kernel/irq/irq_math.c',
            'kernel/kbd_inject.c', 'mm/kmalloc.c', 'sdk/gen_kapi.py',
            'sdk/kapi_abi_x86.py', 'sdk/target_profile.py',
            'tools/tests/kbd_inject_host.c', 'tools/tests/syscall_source.py',
            'tools/tests/test_kbd_inject.py', 'kernel/ime/**', 'input/hal/**', 'tools/tests/input_m7_3_host.c',
            'tools/tests/test_input_m7_3.py', 'tools/tests/ime_policy_host.h',
            'tools/tests/test_kapi_audit.py', 'tools/tests/kapi_audit_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'tools/tests/exec_source.py', 'tools/tests/test_exec_r1.py',
            'tools/tests/*fixture*.h', 'tools/tests/host*.h', 'tools/tests/host_arch/**',
            'include/**', 'exec/**', 'mm/*.h', 'drivers/input/**',
            'drivers/sound/fm/**', 'drivers/char/**', 'kernel/console/**',
            'kernel/input*.c', 'kernel/port_service.c', 'kernel/serial_diag.c',
            'kernel/time/clock_service.c', 'platform/pc98/**', 'lib/**',
            'kapi/kapi_generated.c', 'sdk/include/**'])
import sys
from types import SimpleNamespace
import test_kapi_audit as audit

MUTATIONS = [
    ('composition publication', 5,
     'kbd_policy_ime_state(active, current.owner, current.command_gen);',
     'kbd_policy_ime_state(0 * active, current.owner, current.command_gen);'),
    ('pending consumption', 5,
     'if (!input_ime_cancel_take(&current)) ime_cancel_input();',
     'if (!input_ime_cancel_take(&current)) (void)0;'),
    ('unsafe pending consumption', 5,
     'if (!_irq_enabled() || input_context_snapshot(&current)) return;',
     'if (input_context_snapshot(&current)) return;'),
]
def run(ctx):
    audit.run('qemu', input_m7=True)
    if ctx.mutate:
        for mutation in MUTATIONS:
            audit.run('qemu', mutation, input_m7=True)
    print('PASS M7[3] real IME/context/gate', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(mutate='--mutate' in sys.argv)))
