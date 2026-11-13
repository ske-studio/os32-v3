"""Finite cooperative RX and bounded e82557 IRQ rescue, without libc."""
CHECK = dict(name='net-rx-work-host', tier=4, needs=[], budget=8,
    covers=['net/nic.c', 'net/nic.h', 'net/nic_rx.c', 'net/nic_rx.h',
            'net/rx_work.c', 'net/rx_work.h', 'net/link.h',
            'kernel/idle_work.c', 'kernel/idle_work.h', 'kernel/irq/tick.h',
            'kernel/irq/tick_budget.h', 'kernel/irq/irq.h',
            'include/kernel_tick.h', 'include/clock_source.h', 'include/types.h',
            'include/net_eth.h', 'include/kprintf.h', 'lib/kstring.h',
            'platform/pc98/tick_order.h', 'sdk/include/os32/os32_kapi_shared.h',
            'tools/tests/net_rx_work_host.c', 'tools/tests/test_net_rx_work.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk'])
from pathlib import Path
import os
import subprocess
import sys
import tempfile
from host32 import run as host_run
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('net/rx_work.c', 'b->used >= b->max_frames', 'b->used > b->max_frames'),
    ('net/rx_work.c', '(u32)(now - b->start) >= b->limit_us',
     '(u32)(now - b->start) > b->limit_us'),
    ('net/nic.c', '!_irq_enabled() || kctx_irq_depth != 0', 'kctx_irq_depth != 0'),
    ('net/nic.c', '!_irq_enabled() || kctx_irq_depth != 0', '!_irq_enabled()'),
    ('net/nic.c', '!bound || ticking || unbind_due', '!bound || unbind_due'),
    ('net/nic.c', 'if (!batch_depth++)', 'if (++batch_depth)'),
    ('net/nic.c', 'if (!ticking) return 0;', 'return 0;'),
    ('net/nic.c', 'if (link_due && bound)', 'if (0 && link_due && bound)'),
    ('net/nic.c', '(u32)(tick_count - last_idle_jiffy) >= LINK_RX_IDLE_RESCUE_JIFFIES', '1'),
    ('net/nic.c', 'owner && owner == adapters[NET_NIC_E82557]', 'owner'),
    ('net/nic.c', 'if (idle) last_idle_jiffy = tick_count;', 'if (idle) last_idle_jiffy += 0;'),
    ('net/nic.c', 'last_idle_jiffy = tick_count;\n            bound = ops;',
     'last_idle_jiffy = 0;\n            bound = ops;'),
    ('net/nic.c', 'if (ticking) {\n        unbind_due = 1;', 'if (0) {\n        unbind_due = 1;'),
    ('net/nic_rx.c', 'if (!tick_budget_take(', 'if (0 && !tick_budget_take('),
)


def verify(work, mutation=None):
    for name in ('nic.c', 'nic_rx.c', 'rx_work.c'):
        path = 'net/' + name
        source = (ROOT / path).read_text()
        if mutation and mutation[0] == path:
            assert source.count(mutation[1]) == 1, mutation
            source = source.replace(mutation[1], mutation[2])
        dest = work / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(source)
    exe = work / 'host'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                    '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                    '-D__cdecl=', '-I' + str(work), *include_flags(ROOT),
                    str(ROOT / 'tools/tests/net_rx_work_host.c'), '-o', str(exe)], check=True)
    return host_run([str(exe)], cwd=work, timeout=5).returncode


def main(mutate=False):
    with tempfile.TemporaryDirectory(prefix='net-rx-work-') as td:
        work = Path(td)
        rc = verify(work)
        assert rc == 0, ('control', rc)
        print('GREEN cooperative RX, batch, idle, reentry, rescue, wrap', flush=True)
        if mutate:
            for mutation in MUTATIONS:
                rc = verify(work, mutation)
                assert rc == 1, (mutation, rc)
                print('RED runtime', mutation[1], 'rc=', rc, flush=True)
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
