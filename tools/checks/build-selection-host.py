#!/usr/bin/env python3
CHECK = dict(name='build-selection-host', tier=1,
    covers=['sys/kern/exec/absent/exec_ime.c', 'sys/platform/powermac/fep_imports.c', 'sys/platform/pc98/module_pair.c', 'sys/dev/block/loop_dev.c', 'sys/dev/loop/device_loop.c', 'sys/kern/fep_boot.c', 'sys/kern/ime/fep_module.c', 'sys/dev/video/kcg/kcg.c', 'sys/dev/video/kcg/kcg_module.c', 'sys/dev/kcg/device_kcg.c', 'sys/arch/ppc/sqlite_stack_ppc.S', 'sys/kern/sqlite_engine.c', 'sys/kern/sqlite_outer.c', 'lib/sqlite3/os32_sqlite_test.c', 'lib/sqlite3/sqlite_stack.asm', 'tools/tests/fixtures/build_profile_ppc.json', 'tools/tests/build_fixture.py', 'sdk/target_profile.py', 'sdk/target_profile_cli.py', 'sys/kern/exec/ring3_ls.c', 'sys/arch/ppc/video_wc.c', 'sys/dev/video/geforce/gf_wc.c', 'tools/tests/fixtures/build_profile_x86.json', 'sys/arch/ppc/entry.S', 'sys/arch/ppc/ofw_note.S', 'lib/divdi3.c', 'sys/kern/kstring_c.c', 'tools/build_profile.py', 'Makefile', 'build/**/*.mk', 'tools/tests/test_build_selection.py'], needs=[], budget=5.1,
    basis=['AC-BUILD-1'],
)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_build_selection.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
