#!/usr/bin/env python3
CHECK = dict(name='build-selection-host', tier=1,
    covers=['sdk/target_profile.py', 'sdk/target_profile_cli.py', 'exec/ring3_ls.c', 'arch/ppc/video_wc.c', 'drivers/video/geforce/gf_wc.c', 'tools/tests/fixtures/build_profile_x86.json', 'arch/ppc/entry.S', 'arch/ppc/ofw_note.S', 'lib/divdi3.c', 'lib/kstring_c.c', 'tools/build_profile.py', 'Makefile', 'build/**/*.mk', 'tools/tests/test_build_selection.py'], needs=[], budget=5.1)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_build_selection.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
