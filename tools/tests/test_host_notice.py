#!/usr/bin/env python3
"""B4: caller validation, two-phase notice consumption and serial."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

CHECK = dict(name='host-notice', tier=1, needs=[], covers=[
    'kapi/kapi_host_notice.c', 'kapi/kapi_host_notice.h', 'net/host_mailbox.c',
    'net/host_mailbox.h', 'include/redir_access.h',
    'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/host_notice_host.c', 'tools/tests/hostsvc_fixture.c',
    'tools/tests/hostsvc_fixture.h', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'build/config.mk', 'include/io.h',
    'lib/kstring.h', 'mm/kmalloc.h'], budget=10)
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('copy-failure-consumes', '        return OS32_ERR_INVAL;\n    host_mailbox_commit',
     '        host_mailbox_commit(notice.host_generation, notice.serial);\n    host_mailbox_commit'),
    ('wrong-token-generation', 'host_mailbox_commit(notice.host_generation, notice.serial);',
     'host_mailbox_commit(generation, notice.serial);'),
    ('serial-constant', 'return host_mailbox_latest_serial();',
     'return host_mailbox_latest_serial() * 0;'),
)

def build(work, source):
    exe = work / 'notice'
    result = host32.build(['gcc', '-m32', '-std=gnu11', '-O0', '-Wall', '-Wextra',
        '-Werror', '-fno-pie', '-no-pie', '-fno-stack-protector', '-ffreestanding',
        '-nostdlib', '-static', '-I' + str(ROOT), *include_flags(ROOT),
        '-DPRODUCT_SOURCE="' + str(source) + '"',
        str(ROOT / 'tools/tests/host_notice_host.c'),
        str(ROOT / 'tools/tests/hostsvc_fixture.c'), '-o', str(exe)],
        capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return exe

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='host-notice-') as tmp:
        work = Path(tmp)
        source = ROOT / 'kapi/kapi_host_notice.c'
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', *include_flags(ROOT), '-c', str(source), '-o', str(work / 'target.o')], check=True)
        assert host32.run([str(build(work, source))], timeout=30).returncode == 0
        print('PASS host notice (runner=' + host32.selected_runner() + ')', flush=True)
        if args.mutate:
            original = source.read_text()
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, 'anchor must be unique')
                mutant = work / 'mutant.c'
                # Preserve relative includes for the substituted product source.
                mutant.write_text(original.replace('#include "kapi_host_notice.h"',
                    '#include "' + str(ROOT / 'kapi/kapi_host_notice.h') + '"').replace(
                    '#include "../net/host_mailbox.h"',
                    '#include "' + str(ROOT / 'net/host_mailbox.h') + '"').replace(old, new))
                rc = host32.run([str(build(work, mutant))], timeout=30).returncode
                assert rc == 1, (name, rc)
                print('RED ' + name + ' rc=' + str(rc), flush=True)
    return 0

def run(ctx):
    for index, runner in enumerate(ctx.runners):
        env = dict(os.environ, HOST32_RUNNERS=runner)
        argv = [sys.executable, '-B', 'tools/tests/test_host_notice.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=env)
        if rc:
            return rc
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
