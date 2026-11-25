"""V1 fan support classification and side-effect-free preflight."""
CHECK = dict(name='gf5-fancheck', tier=1, needs=['make:build/out/geforce/geforce_imports.h'], budget=4.2,
             # Includes inputs reached through the shared test_gf5 runner.
             covers=['sys/dev/pci/pci_barsize.c', 'sys/dev/pci/pci_barsize.h', 'tools/tests/recover_gf_dev_stub.c', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h', 'tools/tests/host_exec_context.h',
                 'hwdev/gf/**',
                 'tools/tests/gf5*',
                 'tools/tests/test_gf5_fancheck.py',
                 'tools/tests/test_gf5.py',
                 'tools/tests/gf_startlog_host.h',
                 'tools/tests/host32.py',
                 'tools/tests/mutpar.py',
                 'tools/tests/kernel_includes.py',
                 'tools/tests/gf4_display_fake.h',
                 'tools/tests/gf_accel_host.h',
                 'sys/include/types.h',
                 'sys/include/kprintf.h',
                 'sys/dev/video/geforce/diag/gfinit_decode.c',
                 'sys/dev/video/geforce/diag/gfinit_exec.c',
                 'sys/dev/video/geforce/diag/gfinit_pll.c',
                 'sys/platform/pc98/kdebug/recover.c',
                 'sys/dev/video/geforce/diag/recover_gf.c',
                 'tools/tests/gf4_c2linit_host.c',
                 'tools/tests/gf_runner_host.c',
                 'tools/tests/gfinit_exec_host.c',
                 'tools/tests/gfinit_host.c',
                 'tools/tests/recover_gf_host.c',
                 'tools/tests/test_gf_runner.py',
                 'sys/dev/base/dev.c',
                 'sys/fs/vfs.c',
                 'sys/dev/video/geforce/gf_load_state.c',
                 'sys/dev/video/geforce/diag/gf_display_test.c',
                 'sys/dev/video/geforce/diag/gf_stage_state.c',
                 'sys/dev/video/geforce/diag/gfinit_bus.c',
                 'sys/dev/video/geforce/diag/gfinit_mode.c',
                 'sys/dev/video/geforce/diag/gfinit_plan.c',
                 'sys/dev/video/geforce/diag/gfinit_plan_c.c',
                 'sys/dev/video/geforce/diag/gfinit_resources.c',
                 'sys/arch/x86/kdebug/kdebug_window.c',
                 'sys/platform/pc98/kdebug/recover_profile.c',
                 'sys/kern/module/module_image.c',
                 'sys/kern/module/module_runtime.c',
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
                 'sys/dev/video/geforce/diag/gf5_test.c',
                 'kdebug/gfinit_pll.*',
                 'kdebug/gfinit_decode*',
                 'tools/tests/gf_runner*',
                 'tools/tests/test_gf_display.py',
                 'tools/tests/gf4*',
                 'tools/tests/serial_diag_host.h',
                 'tools/tests/host_arch/*',
                 'sdk/rust/os32api/src/av.rs', 'tools/tests/os32api_host.py',
                 'tools/tests/g16_fixture.py',
                 'drivers/video/geforce/*',
                 'exec/*',
                 'gfx/*',
                 'kernel/*',
                 'sys/platform/pc98/input/kbd_fgpoll.inc',
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
             ],
    basis=['GF_FEAT5_DESIGN'],
)
import subprocess
import sys
import test_gf5 as suite

MUTATIONS = [
    ('fancheck-write', 'sys/dev/video/geforce/therm.c',
     'rc=th_policy(g,&policy);', 'rc=th_init(g); if (!rc) rc=th_policy(g,&policy); else policy.trip=0;', 'fancheck'),
    ('fancheck-absence', 'sys/dev/video/geforce/therm.c',
     'if (rc==GF5_ENODEV) { rc=0; g->source_rc=0; }',
     'if (false && rc==GF5_ENODEV) { rc=0; g->source_rc=0; }', 'fancheck'),
    ('fancheck-route', 'sys/dev/video/geforce/therm.c',
     'if ((log[0]&2U) || (*line!=2 && *line!=9)) return GF5_ENODEV;',
     'if ((log[0]&2U) || (*line!=2 && *line!=9)) return GF5_ENOTSUP;', 'fancheck'),
    ('fancheck-version', 'sys/dev/video/geforce/therm.c',
     'if (r[0]==0x10U) {',
     'if (r[0]!=0x10U) return GF5_ENOTSUP;\n        if (r[0]==0x10U) {', 'fancheck'),
    ('fancheck-free', 'sys/dev/video/geforce/therm.c',
     'if (policy.trip) g->links.free(g->links.owner,policy.trip);',
     'if (false && policy.trip) g->links.free(g->links.owner,policy.trip);', 'fancheck'),
]

def main():
    suite.CASES = {'fancheck': (3, ['sys/dev/video/geforce/therm.c'])}
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
