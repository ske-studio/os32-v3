"""A8 real link.c TX/credit/tick deadline integration, libc-free ILP32."""
CHECK = dict(name='hostsvc-link-core', tier=1, needs=[], budget=5,
    covers=['net/link_notify.c', 'net/link_notify.h', 'net/host_mailbox.h',
            'sdk/include/os32/os32_host_notice.h', 'net/link.c', 'net/nic.h', 'net/link.h', 'net/link_host_api.inc',
            'net/link_rx.inc', 'net/link_selftest.inc', 'lib/le_internal.h',
            'include/types.h', 'include/net_eth.h', 'include/kprintf.h',
            'lib/kstring.h', 'drivers/net/ne2000/ne2000.h',
            'sdk/include/os32/os32_kapi_shared.h', 'build/config.mk',
            'tools/tests/kernel_includes.py', 'tools/tests/host32.py',
            'tools/tests/hostsvc_link_core_host.c'])
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
ANCHOR = '    link_host_deadlines();'


MUTATIONS = (
    ('net/link_host_api.inc', '            e->rel_tick = e->last_tx_tick;',
     '            e->rel_tick = tick_count;'),
    ('net/link_host_api.inc', '        e->rel_pending = 1;\n        e->rel_rid = e->rid;',
     '        e->rel_pending = 0;\n        e->rel_rid = e->rid;'),
    ('net/link_host_api.inc', '            e->rel_interval = LINK_PROBE_TICKS;',
     '            e->rel_interval = LINK_RTO_TICKS;'),
    ('net/link_host_api.inc', '            e->rel_tries = e->retries;',
     '            e->rel_tries = 0;'),
    ('net/link.c', '(e->rel_interval ? e->rel_interval : LINK_RTO_TICKS)',
     'LINK_RTO_TICKS'),
    ('net/link.c', ANCHOR, '    /* deadline hook omitted */'),
    ('net/link_rx.inc', '    link_host_progress((i32)(e - link_h));\n    e->last_rx_tick', '    e->last_rx_tick'),
    ('net/link_rx.inc', '            link_host_progress((i32)(e - link_h)); /* REQUEST ACK advances */', ''),
    ('net/link_rx.inc', '            link_host_progress((i32)(e - link_h)); /* WDATA ACK advances */', ''),
    ('net/link_rx.inc', '                link_host_progress((i32)(e - link_h)); /* New DATA accepted */', ''),
    ('net/link_rx.inc', 'if (seq == e->ack_seq + 1)', 'if (seq >= e->ack_seq)'),
    ('net/link_rx.inc', 'if (rid != LINK_NOTIFY_RID)', 'if (0 && rid != LINK_NOTIFY_RID)'),
    ('net/link_rx.inc', 'int extended = plen == LINK_HELLO_ES_EXT_LEN;', 'int extended = plen == LINK_HELLO_ES_EXT_LEN && 0;'),
    ('net/link.c', '    link_notify_tick(tick_count);', ''),
    ('net/link.c', '    link_notify_reset();', ''),
)


def build(folder, mutant=None):
    folder.mkdir()
    shutil.copytree(ROOT / 'net', folder / 'net')
    if mutant is not None:
        path, anchor, replacement = MUTATIONS[mutant]
        core = folder / path
        source = core.read_text()
        assert source.count(anchor) == 1, anchor
        core.write_text(source.replace(anchor, replacement))
    exe = folder / 'probe'
    subprocess.run(['gcc', '-std=gnu11', '-m32', '-march=i386', '-O2',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-nostdlib', '-static',
        '-fno-pie', '-no-pie', '-fno-stack-protector', '-ffunction-sections',
        '-fdata-sections', '-Wl,--gc-sections', '-I' + str(folder),
        *include_flags(ROOT), str(ROOT / 'tools/tests/hostsvc_link_core_host.c'),
        '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='hostsvc-core-') as tmp:
        folder = Path(tmp)
        host32.run([str(build(folder / 'control'))], check=True)
        print('GREEN A8/A9 TX/credit, shared RX, HELLO, real NOTIFY, progress and silence deadlines')
        if args.mutate:
            for i, (path, anchor, _) in enumerate(MUTATIONS):
                rc = host32.run([str(build(folder / ('mutant%d' % i), i))]).returncode
                assert rc == 1, (path, anchor, rc)
                print('RED runtime (compiled, unique anchor):', path, anchor.strip())


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', __file__,
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
