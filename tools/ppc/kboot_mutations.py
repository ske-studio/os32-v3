"""Negative-image contracts. H3 must register real fault images explicitly.

A contract requires both a preceding progress marker and a dedicated stop symbol;
startup failures and deadlines never constitute a runtime RED.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Negative:
    fatal: str
    reached: str
    stop: str


NEGATIVES = {
    'fixture-fatal': Negative('FATAL fixture', 'KBOOT OF-OFF calls=1',
                              'fixture_stop'),
    # Real kernel images: make -f build/ppc-kboot.mk NEGATIVE=<n>.
    # 1: unarmed supervisor load from the not-present low megabyte (reason 2 = DSI miss).
    'kernel-dsi': Negative('FATAL trap vector=0x300 reason=0x2 addr=0x80000',
                           'KBOOT MEMORY online=1 heap=1 shm=1', 'ppc_trap_stop'),
    # 2: sc before any user ABI exists (reason 0x108 = SC | unsupported).
    'kernel-sc': Negative('FATAL trap vector=0xc00 reason=0x108 addr=0x0',
                          'KBOOT MEMORY online=1 heap=1 shm=1', 'ppc_trap_stop'),
}
KERNEL_NEGATIVES = {'kernel-dsi': 1, 'kernel-sc': 2}

# Product mutations of the real kernel:
#   (name, file, unique anchor, replacement, meter verdict, serial evidence regex).
# RED needs *both*: the exact verdict of the meter, and the one serial line that
# identifies the cause (every other field still passing). A mutant that fails for
# another reason, fails to build, or times out is a test error, never a RED.
TRAP = r'^KBOOT TRAP dsi=%s isi=%s guard=%s flush=%s$'
KERNEL_MUTATIONS = (
    ('ipi-masked', 'platform/powermac/mpic.c',
     'write_reg(IPI_VECTOR, PRIORITY_ONE | PM_MPIC_IPI_VECTOR);',
     'write_reg(IPI_VECTOR, MASK | PRIORITY_ONE | PM_MPIC_IPI_VECTOR);',
     'milestone 6 order/format', r'^KBOOT MPIC sources=\d+ priority=0 cpu=0 delivery=bad$'),
    ('tpr-high', 'platform/powermac/mpic.c',
     '    write_reg(TPR, 0);\n    return PM_MPIC_INIT_OK;',
     '    write_reg(TPR, 15);\n    return PM_MPIC_INIT_OK;',
     'milestone 6 order/format', r'^KBOOT MPIC sources=\d+ priority=0 cpu=0 delivery=bad$'),
    ('half-rate', 'arch/ppc/kernel_dec.c',
     '    period = tb_hz / tick_hz;', '    period = tb_hz / tick_hz * 2;',
     'ALIVE tick/TB mismatch', r'^KBOOT ALIVE tick_delta=(99\d|100\d) tb_us=20000\d\d of_calls_delta=0$'),
    ('onfault-reason', 'arch/ppc/kernel_trap.c',
     '            reason == probe.reason) {', '            reason != probe.reason) {',
     'forbidden diagnostic', r'^FATAL trap vector=0x300 reason=0x2 addr=0x80000$'),
    ('hpt-not-cleared', 'arch/ppc/kernel_mmu.S',
     '    bl ppc_hpt_clear\n    sync\n', '    sync\n',
     'milestone 5 order/format', TRAP % ('pass', 'pass', 'pass', 'bad')),
    # Not a guard-mapping mutation (D3's kboot_boot.py owns those): this proves
    # the guard word is computed, i.e. a load that does not fault reports bad.
    ('guard-probe-nonfault', 'init/ppc_kernel.c',
     '    int guard = probe_dsi(MEM_STACK_GUARD);',
     '    int guard = probe_dsi((u32)flush_a);',
     'milestone 5 order/format', TRAP % ('pass', 'pass', 'bad', 'pass')),
    ('shm-no-shared-user', 'init/ppc_kernel.c',
     '    int shared_user = paging_boot_user_shared((u32)user_gate) == 0;',
     '    int shared_user = 1; (void)user_gate;',
     'milestone 4 order/format', r'^KBOOT MEMORY online=1 heap=1 shm=0$'),
    ('clock-half-scale', 'platform/powermac/clock.c',
     '((delta % hz) * 1000000) / hz', '((delta % hz) * 500000) / hz',
     'forbidden diagnostic', r'^FATAL kboot clock_rate$'),
    # +85.9 s per read: 50*error would wrap to a tiny value in 32 bits.
    ('clock-jump', 'platform/powermac/clock.c',
     '    unsigned long long delta = read_tb() - origin;',
     '    static unsigned long long skew;\n'
     '    unsigned long long delta = read_tb() - origin + (skew += 8589934600ULL);',
     'forbidden diagnostic', r'^FATAL kboot clock_rate$'),
    ('clock-frozen', 'platform/powermac/clock.c',
     '    unsigned long long delta = read_tb() - origin;',
     '    unsigned long long delta = 0;',
     'forbidden diagnostic', r'^FATAL kboot clock_rate$'),
)

# Audit mutations: the build itself must fail with exactly this audit finding.
AUDIT_MUTATIONS = (
    ('pit-direct', 'init/ppc_kernel.c', '#define KBOOT_ATTR 0x07',
     '#define KBOOT_ATTR (0x07 + 0 * PIT_HZ)', 'PIT_HZ user in the 1 kHz closure'),
    ('pit-via-macro', 'init/ppc_kernel.c', '#define KBOOT_ATTR 0x07',
     '#define KBOOT_ATTR (0x07 + 0 * TICK_MS)', 'PIT_HZ user in the 1 kHz closure'),
    ('float-insn', 'arch/ppc/kernel_probe.S', 'ppc_probe_syscall:\n    sc\n',
     'ppc_probe_syscall:\n    sc\n    fneg 1,1\n', 'floating-point/AltiVec instruction'),
    ('altivec-insn', 'arch/ppc/kernel_probe.S', 'ppc_probe_syscall:\n    sc\n',
     'ppc_probe_syscall:\n    sc\n    vxor 0,0,0\n', 'floating-point/AltiVec instruction'),
)

# Mutate the meter's product lines, then exercise it with a guest that must be
# rejected. Each row names the fixture and the exact outcome the mutant must
# produce: 'ACCEPT' (the invalid guest is accepted) or the *different* rejection
# reason of the next guard in depth. An unchanged reason, a timeout or any other
# failure is a test error, never a RED.
METER_MUTATIONS = (
    ('tick-check', 'abs(ticks*1000-us)*100 <= us*5', 'True', 'tick', 'ACCEPT'),
    ('duration-check', "us >= 2000000", 'True', 'duration', 'ACCEPT'),
    ('tick-rate', 'abs(tick_delta*hz-elapsed*1000)*100 <= elapsed*1000*5+2*hz*100',
     'True', 'slow-tick', 'ACCEPT'),
    ('ready-stop', "require(not halted or bool(msr & 0x8000), 'stopped after READY')",
     "require(True, 'stopped after READY')", 'stop', 'interrupts disabled after READY'),
    ('ee-window', "require(elapsed < hz, 'interrupts disabled after READY')",
     "if elapsed >= hz: break", 'ee-off', 'QMP tick rate'),
)
