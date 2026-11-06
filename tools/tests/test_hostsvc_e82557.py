#!/usr/bin/env python3
"""82557 adapter with libc-free driver and module-reference fakes."""
CHECK = dict(name='hostsvc-e82557', tier=1, needs=[], budget=3, covers=[
    'net/nic_e82557.c', 'net/nic.h', 'include/lan_contract.h',
    'drivers/net/e82557/lan_module.h', 'drivers/net/e82557/e82557_regs.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/hostsvc_e82557_host.c', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'build/config.mk'])
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
    ('tx-result', 'return nic_result(e82557_send(frame, len));',
     'return (e82557_send(frame, len), 0);'),
    ('credit-bytes', 'e82557_rx_free() * E82557_FRAME_MAX', 'e82557_rx_free()'),
    ('module-pin', 'if (!lan_module_session_acquire()) return OS32_ERR_NOSYS;',
     'if (0) return OS32_ERR_NOSYS;'),
    ('recovery-poll', 'static void nic_poll(void) { }',
     'static void nic_poll(void) { e82557_tick(); }'),
)
def build_run(work, source):
    src = work / 'nic_e82557.c'
    src.write_text(source)
    exe = work / 'test'
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT), '-I' + str(ROOT / 'tools/tests'),
                    *include_flags(ROOT), '-nostdlib', '-static', '-no-pie', str(src),
                    str(ROOT / 'tools/tests/hostsvc_e82557_host.c'), '-o', str(exe)], check=True, cwd=ROOT)
    return host32.run([str(exe)], cwd=ROOT).returncode

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'net/nic_e82557.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-e82557-') as tmp:
        work = Path(tmp)
        rc = build_run(work, original)
        if rc:
            print(f'FAIL control runtime rc={rc}', flush=True)
            return 1
        print('GREEN 82557 adapter', flush=True)
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
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_e82557.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
