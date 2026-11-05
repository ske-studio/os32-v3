#!/usr/bin/env python3
"""HW-BASIC log formatting and atomic delivery."""
CHECK = dict(name='hwb-log-host', tier=1,
             covers=['userland/shell/hwb_log.c', 'userland/shell/hwb.h',
                     'kdebug/hwb_wire.h', 'tools/tests/hwb_log_host.c',
                     'tools/tests/hwb_fake.h', 'tools/tests/hwb_host.py',
                     'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
                     'tools/tests/mutpar.py'], needs=[], budget=10)
import argparse
import hwb_host

MUTATIONS = [
    ('split', 'userland/shell/hwb_log.c',
     'backend->line(backend->ctx, b->s);',
     'backend->line(backend->ctx, b->s); backend->line(backend->ctx, b->s);', 'records'),
    ('abort', 'userland/shell/hwb_log.c',
     'case HWB_ABORT: return HWB_EXIT_ABORT;',
     'case HWB_ABORT: return 1;', 'exit'),
    ('escape', 'userland/shell/hwb_log.c',
     'c <= 32U || c == 127U', 'c < 32U || c == 127U', 'records'),
]
def run(ctx):
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_hwb_log.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, env=env)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    hwb_host.verify('tools/tests/hwb_log_host.c')
    if args.mutate:
        raise SystemExit(hwb_host.mutations('tools/tests/hwb_log_host.c', MUTATIONS))
