#!/usr/bin/env python3
"""B2 NOTICE: real notification ordering, control retry and mailbox integration.

NET_HOSTSVC_DESIGN §5/#3/#4/#12; fixture clock starts at 0xFFFFFF00.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

CHECK = dict(name='link-notify', tier=1, needs=[], covers=[
    'net/link_notify.c', 'net/link_notify.h', 'net/link.h', 'net/host_mailbox.c',
    'net/host_mailbox.h', 'sdk/include/os32/os32_host_notice.h',
    'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/link_notify_host.c',
    'tools/tests/hostsvc_fixture.c', 'tools/tests/hostsvc_fixture.h',
    'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk',
    'include/io.h', 'include/types.h', 'include/net_eth.h', 'lib/le_internal.h',
    'lib/kstring.h', 'mm/kmalloc.h'], budget=3.1)
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('duplicate-enqueue', 'if (seq != ack_seq + 1u)',
     'if (seq != ack_seq + 1u && seq != ack_seq)'),
    ('gap-enqueue', 'if (seq != ack_seq + 1u)',
     'if (seq != ack_seq + 1u && seq != ack_seq + 2u)'),
    ('old-generation', '(generation_known && generation != host_generation)',
     '(generation_known && generation != host_generation && 0)'),
    ('reserved', 'le_read16(payload + LINK_NOTIFY_RESERVED_OFF) != 0',
     '(le_read16(payload + LINK_NOTIFY_RESERVED_OFF) != 0 && 0)'),
    ('length', 'length != len - LINK_NOTIFY_DATA_OFF',
     '(length != len - LINK_NOTIFY_DATA_OFF && 0)'),
    ('alive-data', 'le_read32(data) != generation',
     '(le_read32(data) != generation && 0)'),
    ('alive-kind', 'kind == LINK_NOTIFY_ALIVE_KIND;',
     'kind == LINK_NOTIFY_ALIVE_KIND && 0;'),
    ('legacy-alive', 'int alive = extended_hello && service == alive_service &&',
     'int alive = service == 23 &&'),
    ('mailbox-nosys', 'if (rc != 0 && rc != OS32_ERR_NOSYS)', 'if (rc != 0)'),
    ('initial-window', 'want_window = 1; /* Initial', 'want_window = 0; /* Initial'),
    ('window-credit', '#define NOTIFY_WINDOW 4', '#define NOTIFY_WINDOW 3'),
    ('ack-retry', 'if (rc != 0) return;\n        want_ack = 0;',
     'if (rc != 0) want_ack = 0;\n        want_ack = 0;'),
    ('window-retry', 'if (rc != 0) return;\n        want_window = 0;',
     'if (rc != 0) want_window = 0;\n        want_window = 0;'),
    ('ack-number', 'ack_seq = seq;', 'ack_seq = seq - 1u;'),
    ('reset-active', 'active = 0;', 'active = 1;'),
    ('reset-sequence', 'ack_seq = 0;', 'ack_seq += 0;'),
)


def build(work, source):
    exe = work / 'notify'
    result = host32.build(['gcc', '-m32', '-std=gnu11', '-O0', '-Wall', '-Wextra',
        '-Werror', '-fno-pie', '-no-pie', '-fno-stack-protector', '-ffreestanding',
        '-nostdlib', '-static', '-I' + str(ROOT), *include_flags(ROOT),
        '-DPRODUCT_SOURCE="' + str(source) + '"',
        str(ROOT / 'tools/tests/link_notify_host.c'),
        str(ROOT / 'tools/tests/hostsvc_fixture.c'), '-o', str(exe)],
        capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return exe


def runs(exe):
    codes = []
    for args in ([], ['heap-failure']):
        result = host32.run([str(exe), *args], capture_output=True, timeout=30)
        if result.returncode:
            print(result.stderr.decode(), end='', file=sys.stderr)
        codes.append(result.returncode)
    return codes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='link-notify-') as tmp:
        work = Path(tmp)
        source = ROOT / 'net/link_notify.c'
        # Compile real privileged IO path with the kernel compiler as well.
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', *include_flags(ROOT), '-c', str(source),
            '-o', str(work / 'target.o')], check=True)
        assert runs(build(work, source)) == [0, 0], 'normal control failed'
        print('PASS notify order/ACK/WINDOW/mailbox/alive/race/wrap + allocation failure (runner=' +
              host32.selected_runner() + ')', flush=True)
        if args.mutate:
            original = source.read_text()
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, 'anchor must be unique')
                mutant = work / 'link_notify.c'
                mutant.write_text(original.replace(old, new))
                rc = runs(build(work, mutant))
                assert all(code >= 0 for code in rc) and any(code == 1 for code in rc), (name, rc)
                print('RED ' + name + ' rc=' + str(rc), flush=True)
    return 0


def run(ctx) -> int:
    for index, runner in enumerate(ctx.runners):
        env = dict(os.environ, HOST32_RUNNERS=runner)
        argv = [sys.executable, '-B', 'tools/tests/test_link_notify.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=env)
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
