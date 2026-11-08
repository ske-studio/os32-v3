#!/usr/bin/env python3
"""LGY attach transfers shared link ownership and preserves reflection."""
CHECK = dict(name='hostsvc-lgy98-attach', tier=1, needs=[], budget=3.4, covers=[
    'drivers/net/ne2000/lgy98.c', 'drivers/net/ne2000/lgy98.h',
    'drivers/net/ne2000/ne2000.h', 'drivers/net/ne2000/ne2000_regs.h',
    'drivers/char/np2sysp.h', 'net/nic.h', 'net/link.h',
    'kernel/irq/tick.h', 'kernel/irq/irq.h', 'platform/pc98/tick_order.h',
    'include/config.h', 'include/kprintf.h', 'lib/kstring.h',
    'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/hostsvc_fixture.h',
    'tools/tests/hostsvc_fixture.c', 'tools/tests/hostsvc_lgy98_attach_host.c',
    'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk'])
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
MUTATIONS = (
    ('missing-registration',
     'rc = net_adapter_register(NET_NIC_LGY98, &net_lgy98_adapter);',
     'rc = 0;'),
)
def build_run(work, source):
    src = work / 'lgy98.c'
    src.write_text(source)
    exe = work / 'test'
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT), '-I' + str(ROOT / 'tools/tests'),
                    '-I' + str(ROOT / 'drivers/net/ne2000'), *include_flags(ROOT), '-nostdlib', '-static', '-no-pie', str(src),
                    str(ROOT / 'tools/tests/hostsvc_fixture.c'),
                    str(ROOT / 'tools/tests/hostsvc_lgy98_attach_host.c'), '-o', str(exe)], check=True, cwd=ROOT)
    return host32.run([str(exe)], cwd=ROOT).returncode

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'drivers/net/ne2000/lgy98.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-lgy98-attach-') as tmp:
        work = Path(tmp)
        rc = build_run(work, original)
        if rc:
            print(f'FAIL control runtime rc={rc}', flush=True)
            return 1
        print('GREEN LGY attach / shared ownership / reflection', flush=True)
        if args.mutate:
            for name, before, after in MUTATIONS:
                if original.count(before) != 1:
                    raise RuntimeError(f'{name}: mutation anchor not unique')
                rc = build_run(work, original.replace(before, after))
                if rc <= 0:
                    raise RuntimeError(f'{name}: expected runtime RED, rc={rc}')
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0

def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_lgy98_attach.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
