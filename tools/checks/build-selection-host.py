#!/usr/bin/env python3
CHECK = dict(name='build-selection-host', tier=1,
    covers=['exec/ring3_ls.c', 'tools/tests/fixtures/build_profile_x86.json', 'arch/ppc/entry.S', 'arch/ppc/ofw_note.S', 'lib/divdi3.c', 'lib/kstring_c.c', 'tools/build_profile.py', 'Makefile', 'build/**/*.mk', 'tools/tests/test_build_selection.py'], needs=[], budget=5.1)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_build_selection.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
