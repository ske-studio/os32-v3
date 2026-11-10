"""T1 continuous thermal samples, priority, pause edges and maximum fan."""
CHECK = dict(name='gf5-therm-t1', tier=1, needs=['make:all'], budget=4.8,
             # Includes inputs reached through the shared test_gf5 runner.
             covers=['drivers/pci/pci_barsize.c', 'drivers/pci/pci_barsize.h', 'tools/tests/recover_gf_dev_stub.c', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h', 'tools/tests/host_exec_context.h',
                 'hwdev/gf/**',
                 'tools/tests/gf5*',
                 'tools/tests/test_gf5_therm_t1.py',
                 'tools/tests/test_gf5.py',
                 'tools/tests/gf_startlog_host.h',
                 'tools/tests/host32.py',
                 'tools/tests/mutpar.py',
                 'tools/tests/kernel_includes.py',
                 'tools/tests/gf4_display_fake.h',
                 'tools/tests/gf_accel_host.h',
                 'include/types.h',
                 'include/kprintf.h',
                 'drivers/video/geforce/geforce_module.h',
                 'kernel/module/*.h',
                 'include/gfx_hal.h',
                 'include/video_service.h',
                 'kdebug/gfinit_decode.c',
                 'kdebug/gfinit_exec.c',
                 'kdebug/gfinit_pll.c',
                 'kdebug/recover.c',
                 'kdebug/recover_gf.c',
                 'tools/tests/gf4_c2linit_host.c',
                 'tools/tests/gf_runner_host.c',
                 'tools/tests/gfinit_exec_host.c',
                 'tools/tests/gfinit_host.c',
                 'tools/tests/recover_gf_host.c',
                 'tools/tests/test_gf_runner.py',
                 'drivers/base/dev.c',
                 'fs/vfs.c',
                 'hwdev/gf_load_state.c',
                 'kdebug/gf_display_test.c',
                 'kdebug/gf_stage_state.c',
                 'kdebug/gfinit_bus.c',
                 'kdebug/gfinit_mode.c',
                 'kdebug/gfinit_plan.c',
                 'kdebug/gfinit_plan_c.c',
                 'kdebug/gfinit_resources.c',
                 'kdebug/kdebug_window.c',
                 'kdebug/recover_profile.c',
                 'kernel/module/module_image.c',
                 'kernel/module/module_runtime.c',
                 'tools/g16/gen_preload16.py',
                 'tools/gen_g16.py',
                 'tools/tests/gf_accel_c1_host.c',
                 'tools/tests/gf_accel_unbound_stubs.c',
                 'tools/tests/gf_display_host.c',
                 'tools/tests/gfinit_bus_host.c',
                 'tools/tests/gfinit_c_only_host.c',
                 'tools/tests/gfinit_init_host.c',
                 'tools/tests/gfinit_mode_host.c',
                 'tools/tests/gfinit_plan_c_host.c',
                 'tools/tests/gfinit_plan_host.c',
                 'tools/tests/gfinit_s0_host.c',
                 'tools/tests/module_image_host.c',
                 'tools/tests/module_runtime_host.c',
                 'tools/tests/test_gfinit_mode.py',
                 'tools/tests/test_gfinit_plan.py',
                 'tools/tests/test_gfinit_plan_c.py',
                 'tools/tests/test_gfinit_stages.py',
                 'tools/tests/test_module_image.py',
                 'tools/tests/test_module_runtime.py',
                 'tools/tests/gf_guard_fake.h',
                 'kdebug/gf5_test.c',
                 'kdebug/gfinit_pll.*',
                 'kdebug/gfinit_decode*',
                 'tools/tests/gf_runner*',
                 'tools/tests/test_gf_display.py',
                 'tools/tests/gf4*',
                 'tools/tests/serial_diag_host.h',
                 'tools/tests/host_arch/*',
                 'tools/tests/os32api_host.py',
                 'tools/tests/g16_fixture.py',
                 'drivers/video/geforce/*',
                 'exec/*',
                 'gfx/*',
                 'kernel/*',
                 'drivers/input/kbd_fgpoll.inc',
                 'include/**',
                 'lib/**',
                 'platform/pc98/**',
                 'mm/*.h',
                 'fs/*.h',
                 'arch/x86/*.h',
                 'arch/x86/kernel/*.h',
                 'tools/prototypes/geforce_imports.def',
                 'userland/gshell/src/**',
                 'userland/gshell/host/**',
                 'userland/gshell/build.rs',
                 'userland/lib/**',
                 'userland/gshell/Cargo.toml',
                 'userland/gshell/Cargo.lock',
                 'sdk/**',
                 'assets/fonts/**',
                 'tools/fetch_fonts.py',
             ])
import subprocess
import sys
import test_gf5 as suite

MUTATIONS = [
    ('shutdown-reason', 'hwdev/gf/therm.c', 'gf5_stop_request(g,GF5_ESTATE,t.milli_c,g->source_rc);', 'gf5_stop_request(g,0,t.milli_c,g->source_rc);', 'therm_t1'),
    ('boost-auto-mode', 'hwdev/gf/therm.c', 'if (boost_rising && f->mode!=GF5_FAN_AUTO)', 'if (false && boost_rising && f->mode!=GF5_FAN_AUTO)', 'therm_t1'),
    ('heat-edge-only', 'hwdev/gf/therm.c', 'i32 heat_rc=gf5_heat_step(g,&t,now);', 'i32 heat_rc=g->fan.active[1] ? 0 : gf5_heat_step(g,&t,now);', 'therm_t1'),
    ('shutdown-request', 'hwdev/gf/therm.c', 'gf5_stop_request(g,GF5_ESTATE,t.milli_c,g->source_rc);', '(void)g;', 'therm_t1'),
    ('critical-edge', 'hwdev/gf/therm.c', 'g->emergency.pause(g->emergency.owner,f->active[TH_CRITICAL]);', '(void)g->emergency;', 'therm_t1'),
    ('maximum-target', 'hwdev/gf/therm.c', 'if (f->active[TH_BOOST]) { target=TH_PERCENT; immediate=true; }', 'if (false) { target=TH_PERCENT; immediate=true; }', 'therm_t1'),
    ('maximum-restore', 'hwdev/gf/therm.c', 'if (f->active[TH_BOOST]) percent=f->max_duty;', 'if (false) percent=f->max_duty;', 'therm_t1'),
    ('hysteresis-equality', 'hwdev/gf/therm.c', 'else if (active && t.milli_c<=cool) active=false;', 'else if (active && t.milli_c<cool) active=false;', 'therm_t1'),
    ('sensor-loss', 'hwdev/gf/therm.c', '(void)gf5_heat_step(g,0,0);', '(void)g;', 'therm_t1'),
]

original_command=suite.command

def production_command(tree, case, exe):
    return original_command(tree,case,exe)+['-DGF_PRODUCTION_MODULE']

def main():
    suite.command=production_command
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
