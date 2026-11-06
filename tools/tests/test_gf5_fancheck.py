"""V1 fan support classification and side-effect-free preflight."""
CHECK = dict(name='gf5-fancheck', tier=1, needs=[], budget=20.0,
             covers=['hwdev/gf/**', 'tools/tests/gf5*',
                     'tools/tests/test_gf5_fancheck.py', 'tools/tests/test_gf5.py',
                     'tools/tests/gf_startlog_host.h', 'tools/tests/host32.py',
                     'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py',
                     'tools/tests/gf4_display_fake.h', 'tools/tests/gf_accel_host.h',
                     'include/types.h', 'include/kprintf.h',
                     'kdebug/gfinit_decode.c', 'kdebug/gfinit_exec.c',
                     'kdebug/gfinit_pll.c', 'kdebug/recover.c', 'kdebug/recover_gf.c',
                     'tools/tests/gf4_c2linit_host.c', 'tools/tests/gf_runner_host.c',
                     'tools/tests/gfinit_exec_host.c', 'tools/tests/gfinit_host.c',
                     'tools/tests/recover_gf_host.c', 'tools/tests/test_gf_runner.py'])
import subprocess
import sys
import test_gf5 as suite

MUTATIONS = [
    ('fancheck-write', 'hwdev/gf/therm.c',
     'rc=th_policy(g,&policy);', 'rc=th_init(g); if (!rc) rc=th_policy(g,&policy);', 'fancheck'),
    ('fancheck-absence', 'hwdev/gf/therm.c',
     'if (rc) return rc;\n    if ((log[0]&2U)',
     'if (rc) return rc==GF5_ENODEV ? GF5_ENOTSUP : rc;\n    if ((log[0]&2U)', 'fancheck'),
    ('fancheck-route', 'hwdev/gf/therm.c',
     'if ((log[0]&2U) || (*line!=2 && *line!=9)) return GF5_ENOTSUP;',
     'if (false && ((log[0]&2U) || (*line!=2 && *line!=9))) return GF5_ENOTSUP;', 'fancheck'),
    ('fancheck-version', 'hwdev/gf/therm.c',
     'if (r[0]!=0x10U) return GF5_ENOTSUP;',
     'if (false && r[0]!=0x10U) return GF5_ENOTSUP;', 'fancheck'),
    ('fancheck-free', 'hwdev/gf/therm.c',
     'if (policy.trip) g->links.free(g->links.owner,policy.trip);',
     'if (false && policy.trip) g->links.free(g->links.owner,policy.trip);', 'fancheck'),
]

def main():
    suite.CASES = {'fancheck': (3, ['hwdev/gf/therm.c'])}
    suite.MUTATIONS = MUTATIONS
    sys.argv += ['--case', 'fancheck']
    return suite.main()


def run(ctx):
    import os
    args = [sys.executable, '-B', 'tools/tests/test_gf5_fancheck.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)


if __name__ == '__main__':
    raise SystemExit(main())
