#!/usr/bin/env python3
CHECK = dict(name='build-selection-host', tier=1,
    covers=['drivers/block/loop_dev.c', 'kernel/dev/device_loop.c', 'kernel/ime/fep_module.c', 'drivers/video/kcg/kcg.c', 'drivers/video/kcg/kcg_module.c', 'kernel/dev/device_kcg.c', 'arch/ppc/sqlite_stack_ppc.S', 'kernel/sqlite_engine.c', 'kernel/sqlite_outer.c', 'lib/sqlite3/os32_sqlite_test.c', 'lib/sqlite3/sqlite_stack.asm', 'tools/tests/fixtures/build_profile_ppc.json', 'tools/tests/build_fixture.py', 'sdk/target_profile.py', 'sdk/target_profile_cli.py', 'exec/ring3_ls.c', 'arch/ppc/video_wc.c', 'drivers/video/geforce/gf_wc.c', 'tools/tests/fixtures/build_profile_x86.json', 'arch/ppc/entry.S', 'arch/ppc/ofw_note.S', 'lib/divdi3.c', 'lib/kstring_c.c', 'tools/build_profile.py', 'Makefile', 'build/**/*.mk', 'tools/tests/test_build_selection.py'], needs=[], budget=5.1,
    basis=['AC-BUILD-1'],
)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_build_selection.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
