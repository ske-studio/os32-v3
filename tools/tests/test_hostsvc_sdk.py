#!/usr/bin/env python3
"""S1-S3 SDK: real host_call transport and generation-scoped LIST cache."""
CHECK = dict(name='hostsvc-sdk', tier=1, needs=[], budget=3.5, covers=[
    'userland/lib/host/host_call.c', 'userland/lib/host/host_call_internal.h',
    'userland/lib/host/libos32host.h',
    'sdk/include/os32/os32api.h', 'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_host_notice.h',
    'include/types.h', 'tools/tests/hostsvc_sdk_host.c', 'tools/tests/host32.py'])

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('whole-deadline', 'return timeout && (u32)(kapi->get_tick() - start) >= ticks;',
     'return timeout && (u32)(kapi->get_tick() - start) > ticks;'),
    ('no-wait', 'if (!timeout) return OS32_ERR_AGAIN;',
     'if (!timeout) return OS32_ERR_TIMEOUT;'),
    ('capacity', 'if (n > b->cap - b->used) return OS32_ERR_NOSPC;',
     'if (n > b->cap - b->used) return 0;'),
    ('close', 'kapi->host_close(h);', '(void)h;'),
    ('write-offset', 'sent += (u32)rc;', 'sent = in_len;'),
    ('cache', 'if (!notice_list) { rc = notice_load();',
     'notice_clear(); if (!notice_list) { rc = notice_load();'),
    ('stale', 'else if (rc == OS32_ERR_STALE) { notice_clear(); have_alive = 0; continue; }',
     'else if (rc == OS32_ERR_STALE) { return rc; }'),
    ('alive', 'notice_clear();\n                rc = notice_load();',
     'rc = 0;'),
)


def build_run(work, source):
    src = work / 'host_call.c'
    src.write_text(source)
    exe = work / 'sdk-test'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
        '-D__OS32_USERLAND__', '-D__cdecl=',
        *['-I' + str(ROOT / p) for p in ('include', 'sdk/include/os32', 'userland/lib/host')],
        '-DPRODUCT_SOURCE="' + str(src) + '"',
        str(ROOT / 'tools/tests/hostsvc_sdk_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)], cwd=ROOT, timeout=30).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'userland/lib/host/host_call.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-sdk-') as tmp:
        work = Path(tmp)
        # Compile public declarations as C89, independently of implementation.
        header = work / 'public.c'
        header.write_text('#include "libos32host.h"\n')
        subprocess.run(['gcc', '-m32', '-std=c89', '-pedantic-errors',
            '-D__OS32_USERLAND__', '-fsyntax-only', '-I' + str(ROOT / 'include'),
            '-I' + str(ROOT / 'sdk/include/os32'), '-I' + str(ROOT / 'userland/lib/host'),
            str(header)], check=True)
        rc = build_run(work, source)
        if rc:
            raise RuntimeError(f'control failed at runtime: rc={rc}')
        print('GREEN SDK transfer/absolute deadline/nonblocking/LIST/cache/STALE/ALIVE/C89', flush=True)
        if args.mutate:
            for name, before, after in MUTATIONS:
                if source.count(before) != 1:
                    raise RuntimeError(f'{name}: mutation anchor not unique')
                rc = build_run(work, source.replace(before, after))
                if rc <= 0:
                    raise RuntimeError(f'{name}: expected runtime RED, rc={rc}')
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_sdk.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
