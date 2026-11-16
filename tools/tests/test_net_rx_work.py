"""Finite cooperative RX and bounded e82557 IRQ rescue, without libc."""
CHECK = dict(name='net-rx-work-host', tier=4, needs=[], budget=8,
    covers=['tools/tests/rx_io_clock.h', 'tools/tests/idle_io_sources.py', 'kernel/io_work.[ch]',
            'kernel/io_clock.h', 'kernel/io_policy.[ch]', 'exec/appslot.h',
            'kernel/time/tick_clock.h', 'net/link_platform.h', 'platform/pc98/link_platform_inline.h', 'net/nic.c', 'net/nic.h', 'net/nic_rx.c', 'net/nic_rx.h',
            'net/rx_work.c', 'net/rx_work.h', 'net/link.h',
            'kernel/idle_work.c', 'kernel/idle_work.h', 'kernel/irq/tick.h',
            'kernel/irq/tick_budget.h', 'kernel/irq/tick_budget.c',
            'arch/x86/arch_tick_budget.h', 'kernel/irq/irq.h',
            'include/kernel_tick.h', 'include/clock_source.h', 'include/types.h',
            'include/net_eth.h', 'include/kprintf.h', 'lib/kstring.h',
            'platform/pc98/tick_order.h', 'sdk/include/os32/os32_kapi_shared.h',
            'tools/tests/net_rx_work_host.c', 'tools/tests/test_net_rx_work.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk'],
    basis=['TICK_T123_DESIGN §3'],
    pins=['LAN_BULK_DESIGN'],
)
from pathlib import Path
from idle_io_sources import idle_io_sources
import os
import subprocess
import sys
import tempfile
from host32 import run as host_run
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('net/nic_rx.c', 'if (len < ETH_HLEN || len > sizeof(rx_frame)) continue;',
     'net_rx_note(); if (len < ETH_HLEN || len > sizeof(rx_frame)) continue;'),
    ('net/nic.c', '(u32)(tick_clock_ms() - last_rx_ms) < window_ms', '(u32)(tick_clock_ms() - last_rx_ms) <= window_ms'),
    ('net/nic.c', '(u32)(tick_clock_ms() - last_rx_ms) < window_ms', '(i32)(tick_clock_ms() - last_rx_ms) < (i32)window_ms'),
    ('net/nic.c', 'idle_ms_valid = 0;\n            rx_ms_valid = 0;', 'idle_ms_valid = 0;'),
    ('net/nic.c', '    rx_ms_valid = 0;\n    owner = 0;', '    owner = 0;'),
    ('net/nic_rx.c', '        net_rx_note();', '        (void)0;'),
    ('net/nic.c', '(u32)(tick_clock_ms() - last_rx_ms) < window_ms', '(u32)(tick_clock_ms() - last_rx_ms) >= window_ms'),
    ('net/nic.c', 'last_rx_ms = tick_clock_ms();', 'last_rx_ms = 0;'),
    ('net/nic.c', 'rx_ms_valid = 1;', 'rx_ms_valid = 0;'),
    ('net/nic.c', 'else if (net_rx_recent(NET_RX_ACTIVE_MS))', 'else if (0)'),
    ('kernel/io_work.c', 'if (idle_work_compat_count() == 0 && !any_pending()) return 0;',
     'if (idle_work_compat_count() == 0 && !any_pending()) { int i = find(IO_SVC_NET_RX); if (i < 0) return 0; io_request(IO_SVC_NET_RX); }'),
    ('net/nic.c', 'io_request(IO_SVC_NET_RX); /* bind */', '(void)0; /* bind */'),
    ('net/nic.c', 'io_request(IO_SVC_NET_RX); /* deferred unbind */', '(void)0;'),
    ('net/nic.c', 'io_request(IO_SVC_NET_RX); /* unbind */', '(void)0;'),
    ('net/nic.c', 'io_request(IO_SVC_NET_RX); /* TX completion/cooperative point */', '(void)0;'),
    ('net/nic.c', 'io_request(IO_SVC_NET_RX); /* IRQ mark or exhausted receive scan */', '(void)0;'),
    ('net/nic.c', 'if (bound) io_request(IO_SVC_NET_RX); /* net_tick, including deferred poll */', '(void)0;'),
    ('net/nic.c', 'if (bound) io_request(IO_SVC_NET_RX); /* net_jiffy, including link_due */', '(void)0;'),
    ('net/nic.c', 'return bound ? IO_MORE : IO_DONE;', 'return bound ? IO_WAIT : IO_DONE;'),
    ('net/nic.c', 'if (ticking || batch_depth) return IO_MORE;', 'if (ticking || batch_depth) return IO_WAIT;'),

    ('net/rx_work.c', '!b->slice || !io_slice_take(b->slice)', '!b->slice'),
    ('net/nic.c', '!_irq_enabled() || kctx_irq_depth != 0', 'kctx_irq_depth != 0'),
    ('net/nic.c', '!_irq_enabled() || kctx_irq_depth != 0', '!_irq_enabled()'),
    ('net/nic.c', '!bound || ticking || unbind_due', '!bound || unbind_due'),
    ('net/nic.c', 'if (!batch_depth++)', 'if (++batch_depth)'),
    ('net/nic.c', 'if (!ticking) return 0;', 'return 0;'),
    ('net/nic.c', 'if (link_due && bound)', 'if (0 && link_due && bound)'),
    ('net/nic.c', 'if (bound && ticking) poll_due = 1;', 'if (bound && ticking) poll_due = 0;'),
    ('net/nic.c', 'if (poll_due && bound)', 'if (0 && poll_due && bound)'),
    ('net/nic.c', '        poll_due = 0;\n        bound->poll();\n        net_rx_schedule();\n    }',
     '        poll_due = 0;\n        bound->poll();\n        bound->poll();\n        net_rx_schedule();\n    }'),
    ('net/nic.c', '(u32)(link_platform_now() - last_idle_jiffy) >= LINK_RX_IDLE_RESCUE_JIFFIES', '1'),
    ('net/nic.c', 'owner && (owner == adapters[NET_NIC_E82557] ||\n                (LINK_PLATFORM_TICK_RX_LGY98_SLOT && owner == adapters[NET_NIC_LGY98]))', 'owner'),
    ('net/nic.c', 'if (idle) last_idle_jiffy = link_platform_now();', 'if (idle) last_idle_jiffy += 0;'),
    ('net/nic.c', 'last_idle_jiffy = link_platform_now();\n            bound = ops;',
     'last_idle_jiffy = 0;\n            bound = ops;'),
    ('net/nic.c', 'if (ticking) {\n        unbind_due = 1;', 'if (0) {\n        unbind_due = 1;'),
    ('net/nic_rx.c', 'if (!(tick_budget_peek(', 'if (0 && !(tick_budget_peek('),
    ('net/nic.c', 'tick_budget_peek(', 'tick_budget_take('),
)


def verify(work, mutation=None, link=False):
    for name in ('nic.c', 'nic_rx.c', 'rx_work.c'):
        path = 'net/' + name
        source = (ROOT / path).read_text()
        if mutation and mutation[0] == path:
            assert source.count(mutation[1]) == 1, mutation
            source = source.replace(mutation[1], mutation[2])
        dest = work / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(source)
    # Override only the platform scheduling constant, retaining PC-98 host IRQs.
    header = (ROOT / 'platform/pc98/link_platform_inline.h').read_text()
    anchor = '#define LINK_PLATFORM_TICK_RX_LGY98_SLOT 0'
    assert header.count(anchor) == 1
    (work / 'link_platform_inline.h').write_text(header.replace(
        anchor, '#define LINK_PLATFORM_TICK_RX_LGY98_SLOT ' + str(int(link))))
    product = (ROOT / 'kernel/io_work.c').read_text()
    if mutation and mutation[0] == 'kernel/io_work.c':
        assert product.count(mutation[1]) == 1
        product = product.replace(mutation[1], mutation[2])
    exe = work / 'host'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                    '-fno-stack-protector', '-nostdinc', '-nostdlib', '-static', '-no-pie',
                    '-D__cdecl=', '-I' + str(work), *include_flags(ROOT),
                    str(ROOT / 'tools/tests/net_rx_work_host.c'), *idle_io_sources(ROOT, work, product), '-o', str(exe)], check=True)
    return host_run([str(exe)], cwd=work, timeout=5).returncode


def main(mutate=False):
    with tempfile.TemporaryDirectory(prefix='net-rx-work-') as td:
        work = Path(td)
        rc = verify(work)
        assert rc == 0, ('control', rc)
        assert verify(work, link=True) == 0
        print('GREEN GEM tick RX and cooperative RX, batch, idle, reentry, rescue, wrap', flush=True)
        if mutate:
            mutation = ('net/nic.c', 'LINK_PLATFORM_TICK_RX_LGY98_SLOT && owner == adapters[NET_NIC_LGY98]',
                        '0')
            assert verify(work, mutation, link=True) == 1
            print('RED runtime GEM tick RX disabled', flush=True)
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
