"""P2 ROM lifetime boundaries; reuse the freestanding ROM host and runner."""
CHECK = dict(name='gf5-rom-guard-host', tier=1, needs=['make:build/out/geforce/geforce_imports.h'], budget=3.9,
             # Includes inputs reached through the shared test_gf5 runner.
             covers=[
                 'tools/tests/test_gf5_rom_guard.py',
                 'tools/tests/test_gf5.py',
                 'tools/tests/gf5_rom_host.c',
                 'tools/tests/gf5_fake.h',
                 'tools/tests/gf4_display_fake.h',
                 'tools/tests/gf_accel_host.h',
                 'tools/tests/host32.py',
                 'tools/tests/mutpar.py',
                 'tools/tests/kernel_includes.py',
                 'hwdev/gf/**',
                 'include/types.h',
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
                 'tools/tests/gf5*',
                 'tools/tests/gf_startlog_host.h',
                 'include/kprintf.h',
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
import test_gf5


MUTATIONS = [
    ('rom-acquire-guard', 'gf5_plan_invalidate(g); /* ROM acquire boundary */',
     '(void)g; /* ROM acquire boundary */'),
    ('rom-release-guard', 'gf5_plan_invalidate(g); /* ROM release boundary */',
     '(void)g; /* ROM release boundary */'),
    ('rom-acquire-generation',
     '++g->rom_generation; /* acquire: wrap is safe with the guard invalid */',
     '(void)g; /* acquire: generation omitted */'),
    ('rom-release-generation',
     '++g->rom_generation; /* release: never revive a guard on wrap */',
     '(void)g; /* release: generation omitted */'),
]


def main():
    # U7 owns test_gf5.py. Add P2 anchors here without editing its registry.
    test_gf5.MUTATIONS = [m for m in test_gf5.MUTATIONS if m[4] == 'rom'] + [
        (name, 'hwdev/gf/feature5_rom.c', old, new, 'rom')
        for name, old, new in MUTATIONS]
    sys.argv.extend(['--case', 'rom'])
    return test_gf5.main()


def run(ctx):
    import os
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = [sys.executable, '-B', 'tools/tests/test_gf5_rom_guard.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    raise SystemExit(main())
