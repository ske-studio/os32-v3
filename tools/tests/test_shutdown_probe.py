"""T2h h3b: guest verdict and real gfx shutdown authority on three backends."""
import argparse
import host32
from test_gfx_kernel_fb import run_case, ROOT

HOOKS = [
    ('gfx/gfx_core.c', 'if (g_backend && g_backend->leave) g_backend->leave();',
     'if (g_backend && g_backend->leave) { extern void host_shutdown_event(int); host_shutdown_event(0); g_backend->leave(); }'),
    ('gfx/gfx_core.c', 'if (g_backend && g_backend->shutdown) g_backend->shutdown();',
     'if (g_backend && g_backend->shutdown) { extern void host_shutdown_event(int); host_shutdown_event(1); g_backend->shutdown(); }'),
]
MUTANTS = [
    ('foreign-allowed', 'caller.app_id != appslot_gfx_owner()', '0'),
    ('owner-denied', 'caller.app_id != appslot_gfx_owner()', 'caller.app_id == appslot_gfx_owner()'),
    ('trusted-denied', 'ring3_call_from_user() && con_sink_is_enabled()', 'con_sink_is_enabled()'),
]


def stage(tmp, sources):
    # This freestanding ILP32 harness provides its own string implementations;
    # do not depend on host multilib libc headers or libraries.
    (tmp / 'string.h').write_text(
        'void *memset(void *, int, __SIZE_TYPE__);\n'
        'int memcmp(const void *, const void *, __SIZE_TYPE__);\n')


@host32.control_session
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=['native', 'qemu'], default='native')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    host32.begin_control(args.mutate, args.runner, ROOT)
    fixture = '#include "shutdown_probe_host.c"\n'
    with host32.control(args.mutate, args.runner, ROOT) as normal:
        if normal:
            result = run_case(None, args.runner, HOOKS, fixture, stage=stage)
            print(result.stdout + result.stderr, end='')
            assert result.returncode == 0, result.returncode
    if args.mutate:
        for name, old, new in MUTANTS:
            result = run_case(None, args.runner, HOOKS + [('gfx/gfx_core.c', old, new)], fixture, stage=stage)
            assert result.returncode == 1 and 'FAIL' in result.stdout, (name, result)
            print('RED ' + name)


if __name__ == '__main__':
    main()
