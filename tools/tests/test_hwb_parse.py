"""HW-BASIC §2 syntax and checked expression evaluation, libc-free ILP32."""
CHECK = dict(name='hwb-parse-host', tier=1, budget=5.8, needs=[],
    covers=['userland/shell/hwb_log.c', 'userland/shell/hwb_probe.c', 'userland/shell/hwb_parse.c', 'userland/shell/hwb.h',
            'kdebug/probe_wire.h', 'kdebug/hwb_wire.h', 'include/types.h',
            'sdk/include/os32/os32_kapi_shared.h',
            'tools/tests/hwb_parse_host.c', 'tools/tests/hwb_fake.h',
            'tools/tests/hwb_host.py', 'tools/tests/host32.py',
            'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
import sys
import hwb_host

FIXTURE = 'tools/tests/hwb_parse_host.c'
PRODUCT = 'userland/shell/hwb_parse.c'
MUTATIONS = [
    ('undefined-label', PRODUCT, 'if (!found) { if (error_line)',
     'if (0 && !found) { if (error_line)', 'undefined-label'),
    ('shift-range', PRODUCT, 'if (b >= 32U)', 'if (b > 32U)', 'shift'),
    ('add-overflow', PRODUCT, 'if (b > U32_MAX - v) p->bad = 1;',
     'if (0 && b > U32_MAX - v) p->bad = 1;', 'overflow'),
    ('duplicate-label', PRODUCT, 'if (hp_equal(source, i.name, g->labels[k].name)) return HWB_ARG;',
     'if (0 && hp_equal(source, i.name, g->labels[k].name)) return HWB_ARG;', 'duplicate-label'),
]


def main():
    hwb_host.verify(FIXTURE)
    if '--mutate' in sys.argv:
        return int(bool(hwb_host.mutations(FIXTURE, MUTATIONS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())


def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_hwb_parse.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)
