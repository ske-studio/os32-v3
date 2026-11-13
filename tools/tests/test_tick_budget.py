"""IRQ0 budget boundaries, context, actual PIT wrap and link tail wiring."""
CHECK = dict(name='tick-budget-host', tier=4, needs=[], budget=3.0,
    covers=['kernel/irq/tick_budget.[ch]', 'arch/x86/arch_tick_budget.h',
            'platform/pc98/tick_budget.c', 'net/link.c', 'net/link_rx.inc',
            'tools/tests/tick_budget_host.c', 'tools/tests/test_tick_budget.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'platform/pc98/pit.h', 'platform/pc98/pit_math.h',
            'include/types.h', 'include/kernel_tick.h', 'include/platform_timer.h',
            'tools/tests/hostsvc_link_core_host.c', 'net/*.[ch]', 'net/*.inc'])
from pathlib import Path
import subprocess
import sys
import tempfile
from host32 import run as host_run
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('net/link.c', 'link_state == LINK_S_SYN && link_hello_idle ? LINK_HELLO_IDLE_TICKS : LINK_RTO_TICKS', '0 ? LINK_HELLO_IDLE_TICKS : LINK_RTO_TICKS'),
    ('net/link_rx.inc', '    link_hello_idle = 0;', '    (void)0;'),
    ('net/link_rx.inc', 'if (op != LINK_OP_HELLO || flags == LINK_HS_SYNACK || flags == LINK_HS_ESTAB)', 'if (1)'),
    ('net/link_rx.inc', 'if (!net_rx_queued(NET_RX_LINK)) break;', 'if (0) break;'),
    ('net/link.c', 'if (!link_tx_pending()) break;', 'if (0 && !link_tx_pending()) break;'),
    ('kernel/irq/tick_budget.c', 'tail_cnt < share_cnt - elapsed', 'tail_cnt <= share_cnt - elapsed'),
    ('kernel/irq/tick_budget.c', '!taken && turn', '!taken && (turn || 1)'),
    ('kernel/irq/tick_budget.c', 'taken = 1;', 'taken = 0;'),
    ('kernel/irq/tick_budget.c', 'if (!active) return 1;', 'if (active < 0) return 1;'),
    ('platform/pc98/tick_budget.c', 'base + reload - now', 'base + (reload & 0) - now'),
    ('net/link.c', 'tick_budget_cnt(NET_TAIL_TX_US)', '0'),
    ('net/link_rx.inc', 'tick_budget_cnt(NET_TAIL_RX_US)', '0'),
)


def verify(work, mutation=None):
    for rel in ('kernel/irq/tick_budget.c', 'platform/pc98/tick_budget.c',
                'net/link.c', *[str(p.relative_to(ROOT)) for p in (ROOT / 'net').glob('*.inc')]):
        source = (ROOT / rel).read_text()
        if mutation and rel == mutation[0]:
            assert source.count(mutation[1]) == 1, mutation
            source = source.replace(mutation[1], mutation[2])
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(source)
    exe = work / 'test'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                    '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-nostdlib', '-static', '-no-pie', '-D__cdecl=', '-I' + str(work),
                    *include_flags(ROOT), str(ROOT / 'tools/tests/tick_budget_host.c'),
                    '-o', str(exe)], check=True)
    return host_run([str(exe)], cwd=work, timeout=3).returncode


def main(mutate=False):
    with tempfile.TemporaryDirectory(prefix='tick-budget-') as td:
        work = Path(td)
        assert verify(work) == 0
        print('PASS IRQ0 budget / PIT wrap / link RX and TX tails', flush=True)
        if mutate:
            for mutation in MUTATIONS:
                rc = verify(work, mutation)
                assert rc != 0, mutation
                print('RED runtime', mutation[1], 'rc=', rc, flush=True)
    return 0


def run(ctx):
    return main(ctx.mutate)


if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
