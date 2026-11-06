"""T1 continuous thermal samples, priority, pause edges and maximum fan."""
CHECK = dict(name='gf5-therm-t1', tier=1, needs=[], budget=20.0,
             covers=['hwdev/gf/**', 'tools/tests/gf5*',
                     'tools/tests/test_gf5_therm_t1.py', 'tools/tests/test_gf5.py',
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
    ('boost-auto-mode', 'hwdev/gf/therm.c',
     'if (boost_rising && f->mode!=GF5_FAN_AUTO)',
     'if (false && boost_rising && f->mode!=GF5_FAN_AUTO)', 'therm_t1'),
    ('heat-edge-only', 'hwdev/gf/therm.c',
     'i32 heat_rc=gf5_heat_step(g,&t,now);',
     'i32 heat_rc=g->fan.active[1] ? 0 : gf5_heat_step(g,&t,now);', 'therm_t1'),
    ('shutdown-request', 'hwdev/gf/therm.c',
     'g->heat.stop_pending=true;\n        g->heat.cool_samples=0;',
     'g->heat.stop_pending=false;\n        g->heat.cool_samples=0;', 'therm_t1'),
    ('critical-edge', 'hwdev/gf/therm.c',
     'else if (critical_changed && g->emergency.pause)',
     'else if (false && critical_changed && g->emergency.pause)', 'therm_t1'),
    ('maximum-target', 'hwdev/gf/therm.c',
     'if (f->active[TH_BOOST]) { target=TH_PERCENT; immediate=true; }',
     'if (false) { target=TH_PERCENT; immediate=true; }', 'therm_t1'),
    ('maximum-restore', 'hwdev/gf/therm.c',
     'if (f->active[TH_BOOST]) percent=f->max_duty;',
     'if (false) percent=f->max_duty;', 'therm_t1'),
    ('hysteresis-equality', 'hwdev/gf/therm.c',
     'else if (active && t.milli_c<cool) active=false;',
     'else if (active && t.milli_c<=cool) active=false;', 'therm_t1'),
    ('sensor-loss', 'hwdev/gf/therm.c',
     '(void)gf5_heat_step(g,0,0);',
     '(void)g;', 'therm_t1'),
]

def main():
    suite.CASES = {'therm_t1': (3, ['hwdev/gf/therm.c'])}
    suite.MUTATIONS = MUTATIONS
    sys.argv += ['--case', 'therm_t1']
    return suite.main()


def run(ctx):
    import os
    args = [sys.executable, '-B', 'tools/tests/test_gf5_therm_t1.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)


if __name__ == '__main__':
    raise SystemExit(main())
